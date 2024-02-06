#include "le_exr.h"
#include "le_log.h"
#include "le_core.h"

#include "private/le_renderer/le_renderer_types.h"
#include "shared/interfaces/le_image_encoder_interface.h"

#include <cassert>
#include <vector>

#include "ImfFrameBuffer.h"
#include "ImfHeader.h"
#include "ImfOutputFile.h"
#include "ImfChannelList.h"

struct le_image_encoder_format_o {
	le::Format format;
};

#if ( WIN32 ) and defined( PLUGINS_DYNAMIC )
#	pragma comment( lib, "bin/modules/OpenEXR-3_3.lib" )
#endif

static auto logger = LeLog( "le_exr" );

// ----------------------------------------------------------------------
// We must give clients of this encoder a chance to check whether they can assume
// a compatible version of this encoder:
static uint64_t le_image_encoder_get_encoder_version( le_image_encoder_o* encoder ) {
	static constexpr uint64_t ENCODER_VERSION = 0ull << 48 | 0ull << 32 | 1ull << 16 | 0ull << 0;
	return 0;
};

// ----------------------------------------------------------------------

static le_exr_image_encoder_parameters_t get_default_parameters() {
	using ns = le_exr_image_encoder_parameters_t;
	return le_exr_image_encoder_parameters_t{ {
			{ ns::eF16, "R", false },
			{ ns::eF16, "G", false },
			{ ns::eF16, "B", false },
			{ ns::eF16, "A", false },
	} };
}

// ----------------------------------------------------------------------

struct le_image_encoder_o {
	uint32_t image_width  = 0;
	uint32_t image_height = 0;

	std::string output_file_name;

	le_exr_image_encoder_parameters_t params = get_default_parameters(); // todo: set defaults
};

// ----------------------------------------------------------------------

static le_image_encoder_o* le_image_encoder_create( char const* file_path, uint32_t width, uint32_t height ) {
	auto self = new le_image_encoder_o();

	self->output_file_name = file_path;
	self->image_width      = width;
	self->image_height     = height;

	return self;
}

// ----------------------------------------------------------------------

static void le_image_encoder_destroy( le_image_encoder_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_image_encoder_set_encode_parameters( le_image_encoder_o* self, void* p_parameters ) {
	if ( p_parameters ) {
		self->params = *static_cast<le_exr_image_encoder_parameters_t*>( p_parameters );
	} else {
		logger.warn( "Could not set parameters for encoder: Parameters pointer was NULL." );
	}
}

// ----------------------------------------------------------------------

static Imf::FrameBuffer framebuffer_from_vk_format( le_image_encoder_format_o* p_format, Imf::Header const& header, uint8_t const* p_pixel_data, size_t image_width ) {

	Imf::FrameBuffer framebuffer;

	le::Format format = p_format->format;

	le_num_type num_type     = {};
	uint32_t    num_channels = {};

	le_format_infer_channels_and_num_type( format, &num_channels, &num_type );

	uint32_t bytes_per_pixel = size_of( num_type ) * num_channels;

	// TODO: we must find out whether we can use the input data to write
	// into the output file...
	//
	// This here is where we should apply any swizzles - bgra argb etc.
	// the swizzles apply to the source data.

	std::array<uint32_t, 4>    channel_idx        = { 0, 1, 2, 3 }; // RBGA -> reorder here to re-order channels
	std::array<std::string, 4> channel_names      = { "R", "G", "B", "A" };
	std::array<size_t, 4>      per_channel_offset = { 0, 1, 2, 3 }; // will get scaled later

	// scale per channel offset by size of pixel type
	for ( auto& o : per_channel_offset ) {
		o *= size_of( num_type );
	}

	Imf::PixelType pixel_type = Imf::NUM_PIXELTYPES;

	switch ( num_type ) {
	case le_num_type::eF16:
		pixel_type = Imf::PixelType::HALF;
		break;
	case le_num_type::eF32:
		pixel_type = Imf::PixelType::FLOAT;
		break;
	case le_num_type::eU32:
		pixel_type = Imf::PixelType::UINT;
		break;
	default:
		logger.error( "Can't write out out exr image from vk image with format: %s", le::to_str( format ) );
		break;
	}

	assert( pixel_type != Imf::NUM_PIXELTYPES );

	if ( num_channels == 1 ) {
		framebuffer.insert(
		    "R",
		    Imf::Slice(
		        pixel_type,
		        ( char* )p_pixel_data,          // base ptr
		        bytes_per_pixel * 1,            // x-stride
		        bytes_per_pixel * image_width ) // y-stride
		);
	} else {
		for ( size_t i = 0; i != num_channels; i++ ) {
			framebuffer.insert(
			    channel_names[ channel_idx[ i ] ],
			    Imf::Slice(
			        pixel_type,
			        ( char* )p_pixel_data + per_channel_offset[ channel_idx[ i ] ], // base ptr
			        bytes_per_pixel * 1,                                            // x-stride
			        bytes_per_pixel * image_width )                                 // y-stride
			);
		}
	}
	return framebuffer;
}

// ----------------------------------------------------------------------

static bool le_image_encoder_write_pixels( le_image_encoder_o* self, uint8_t const* p_pixel_data, size_t pixel_data_byte_count, le_image_encoder_format_o* pixel_data_format ) {

	Imf::Header header( self->image_width, self->image_height );

	using pixel_t = le_exr_image_encoder_parameters_t::PixelType;

	size_t pixel_size  = 0; // size of a pixel
	size_t channel_idx = 0;

	for ( auto& channel_param : self->params.channels ) {
		if ( channel_param.type == pixel_t::eUnused ) {
			continue;
		}
		Imf::Channel channel{};

		if ( channel_param.type == pixel_t::eF16 ) {
			channel.type = Imf::HALF;
		} else if ( channel_param.type == pixel_t::eF32 ) {
			channel.type = Imf::FLOAT;
		} else if ( channel_param.type == pixel_t::eU32 ) {
			channel.type = Imf::UINT;
		}

		channel.pLinear = !channel_param.non_linear;

		header.channels().insert( channel_param.channel_name, channel );
		channel_idx++;
	}

	// Note that the pixel format that you write must match
	// the pixel format of the source vulkan image.

	// This means that the pixel format defined in the header
	// must match the pixel format declared in the framebuffer.
	//
	// Otherwise the OpenEXR encoder will complain.

	auto framebuffer = framebuffer_from_vk_format( pixel_data_format, header, p_pixel_data, self->image_width );

	auto outputfile = Imf::OutputFile( self->output_file_name.c_str(), header );

	outputfile.setFrameBuffer( framebuffer );

	outputfile.writePixels( self->image_height );

	return true;
}

// ----------------------------------------------------------------------

void* le_image_encoder_clone_parameters_object( void* obj ) {
	auto result    = new le_exr_image_encoder_parameters_t{};
	auto typed_obj = static_cast<le_exr_image_encoder_parameters_t*>( obj );
	*result        = *typed_obj;
	return result;
};

// ----------------------------------------------------------------------
void le_image_encoder_destroy_parameters_object( void* obj ) {
	le_exr_image_encoder_parameters_t* typed_obj =
	    static_cast<le_exr_image_encoder_parameters_t*>( obj );
	delete ( typed_obj );
};

// ----------------------------------------------------------------------

void le_register_exr_encoder_api( void* api ) {

	auto& le_image_encoder_i = static_cast<le_exr_api*>( api )->le_exr_image_encoder_i;

	if ( le_image_encoder_i == nullptr ) {
		le_image_encoder_i = new le_image_encoder_interface_t{};
	} else {
		// The interface already existed - we have been reloaded and only just need to update
		// function pointer addresses.
		//
		// This is important as by not re-allocating a new interface object
		// but by updating the existing interface object by-value, we keep the *public
		// address for the interface*, while updating its function pointers.
		*le_image_encoder_i = le_image_encoder_interface_t();
	}

	le_image_encoder_i->clone_image_encoder_parameters_object   = le_image_encoder_clone_parameters_object;
	le_image_encoder_i->destroy_image_encoder_parameters_object = le_image_encoder_clone_parameters_object;

	le_image_encoder_i->create_image_encoder  = le_image_encoder_create;
	le_image_encoder_i->destroy_image_encoder = le_image_encoder_destroy;
	le_image_encoder_i->write_pixels          = le_image_encoder_write_pixels;
	le_image_encoder_i->set_encode_parameters = le_image_encoder_set_encode_parameters;
	le_image_encoder_i->get_encoder_version   = le_image_encoder_get_encoder_version;
}
