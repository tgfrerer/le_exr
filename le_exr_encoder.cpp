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
			{ "R", false },
			{ "G", false },
			{ "B", false },
			{ "A", false },
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

static Imf::FrameBuffer framebuffer_from_vk_format( Imf::Header const& header, uint8_t const* p_pixel_data, size_t image_width, Imf::PixelType pixel_data_type, size_t num_channels ) {

	Imf::FrameBuffer framebuffer;

	size_t size_of_num_type_in_bytes = 0;

	switch ( pixel_data_type ) {
	case ( Imf::FLOAT ):
	case ( Imf::UINT ):
		size_of_num_type_in_bytes = 4;
		break;
	case ( Imf::HALF ):
		size_of_num_type_in_bytes = 2;
		break;
	default:
		logger.error( "Unknown pixel data type: %d", pixel_data_type );
		return framebuffer;
	}

	uint32_t bytes_per_pixel = size_of_num_type_in_bytes * num_channels;

	const std::string names_for_channels = num_channels > 1 ? "RGBA" : "Y";

	std::array<size_t, 4> channel_index = { 0, 1, 2, 3 }; // will get scaled later

	for ( size_t i = 0; i != num_channels; i++ ) {
		framebuffer.insert(
		    std::string( { names_for_channels[ i ] } ),
		    Imf::Slice(
		        pixel_data_type,
		        ( char* )p_pixel_data + ( channel_index[ i ] * size_of_num_type_in_bytes ), // base ptr
		        bytes_per_pixel * 1,                                                        // x-stride
		        bytes_per_pixel * image_width )                                             // y-stride
		);
	}

	return framebuffer;
}

// ----------------------------------------------------------------------

static bool le_image_encoder_write_pixels( le_image_encoder_o* self, uint8_t const* p_pixel_data, size_t pixel_data_byte_count, le_image_encoder_format_o* pixel_data_format ) {

	Imf::Header header( self->image_width, self->image_height );

	Imf::PixelType pixel_type = Imf::PixelType::NUM_PIXELTYPES;

	switch ( pixel_data_format->format ) {
	case ( le::Format::eR32G32B32A32Sfloat ):
	case ( le::Format::eR32G32B32Sfloat ):
	case ( le::Format::eR32Sfloat ):
		pixel_type = Imf::FLOAT;
		break;
	case ( le::Format::eR32G32B32A32Uint ):
	case ( le::Format::eR32G32B32Uint ):
	case ( le::Format::eR32Uint ):
		pixel_type = Imf::UINT;
		break;
	case ( le::Format::eR16G16B16A16Sfloat ):
	case ( le::Format::eR16G16B16Sfloat ):
	case ( le::Format::eR16Sfloat ):
		pixel_type = Imf::HALF;
		break;
	default:
		logger.error( "Unknown or unsupported image format: %s", le::to_str( pixel_data_format->format ) );
		return false;
	}

	// -----------| invariant: format is one of the named above formats

	size_t num_channels_in_source_image = 0;

	switch ( pixel_data_format->format ) {
	case ( le::Format::eR32G32B32A32Sfloat ):
	case ( le::Format::eR32G32B32A32Uint ):
	case ( le::Format::eR16G16B16A16Sfloat ):
		num_channels_in_source_image = 4;
		break;
	case ( le::Format::eR32G32B32Sfloat ):
	case ( le::Format::eR32G32B32Uint ):
	case ( le::Format::eR16G16B16Sfloat ):
		num_channels_in_source_image = 3;
		break;
	case ( le::Format::eR32Sfloat ):
	case ( le::Format::eR32Uint ):
	case ( le::Format::eR16Sfloat ):
		num_channels_in_source_image = 1;
		break;
	default:
		logger.error( "Unknown or unsupported image format: %s", le::to_str( pixel_data_format->format ) );
		return false;
	}

	for ( auto& channel_param : self->params.channels ) {
		if ( channel_param.channel_name[ 0 ] == '\0' ) {
			continue;
		}
		Imf::Channel channel{};

		channel.type    = pixel_type;
		channel.pLinear = !channel_param.non_linear;

		header.channels().insert( channel_param.channel_name, channel );
	}

	auto framebuffer = framebuffer_from_vk_format( header, p_pixel_data, self->image_width, pixel_type, num_channels_in_source_image );

	auto outputfile = Imf::OutputFile( self->output_file_name.c_str(), header );

	outputfile.setFrameBuffer( framebuffer );
	outputfile.writePixels( self->image_height );

	return true;
}

// ----------------------------------------------------------------------

void* le_image_encoder_clone_parameters_object( void* obj ) {
	auto result = new le_exr_image_encoder_parameters_t{
	    *static_cast<le_exr_image_encoder_parameters_t*>( obj ) };
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
	le_image_encoder_i->destroy_image_encoder_parameters_object = le_image_encoder_destroy_parameters_object;

	le_image_encoder_i->create_image_encoder  = le_image_encoder_create;
	le_image_encoder_i->destroy_image_encoder = le_image_encoder_destroy;
	le_image_encoder_i->write_pixels          = le_image_encoder_write_pixels;
	le_image_encoder_i->set_encode_parameters = le_image_encoder_set_encode_parameters;
	le_image_encoder_i->get_encoder_version   = le_image_encoder_get_encoder_version;
}
