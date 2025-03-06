#include "le_exr.h"
#include "le_log.h"
#include "le_core.h"

#include "private/le_renderer/le_renderer_types.h"
#include "shared/interfaces/le_image_decoder_interface.h"

#include <cassert>

#include "ImfFrameBuffer.h"
#include "ImfHeader.h"
#include "ImfInputFile.h"
#include "ImfChannelList.h"

struct le_image_decoder_format_o {
	le::Format format;
};

#if ( WIN32 ) and defined( PLUGINS_DYNAMIC )
#	pragma comment( lib, "bin/modules/OpenEXR-3_3.lib" )
#endif

static auto logger = LeLog( "le_exr" );

// ----------------------------------------------------------------------

struct le_image_decoder_o {
	uint32_t image_width  = 0;
	uint32_t image_height = 0;
	uint32_t channel_count = 0;

	Imf::InputFile* inputFile;

	le::Format image_inferred_format  = le::Format::eUndefined;
	le::Format image_requested_format = le::Format::eUndefined; // requested format wins over inferred format

	le::Format getFormat() {
		return ( image_requested_format != le::Format::eUndefined ) ? image_requested_format : image_inferred_format;
	};
};

// ----------------------------------------------------------------------

static le_image_decoder_o* le_image_decoder_create( char const* file_path ) {
	auto self = new le_image_decoder_o();

	try {

		self->inputFile = new Imf::InputFile( file_path );

		logger.debug( "Opened file: %s", file_path );
		Imath::Box2i dw    = self->inputFile->header().dataWindow();
		self->image_width  = dw.max.x - dw.min.x + 1;
		self->image_height = dw.max.y - dw.min.y + 1;

		auto const& channels = self->inputFile->header().channels();

		Imf::PixelType pixel_type = Imf::PixelType::NUM_PIXELTYPES;

		self->channel_count = 0;

		char channel_names[ 4 ] = {};

		for ( auto c = channels.begin(); c != channels.end(); c++ ) {
			if ( pixel_type == Imf::PixelType::NUM_PIXELTYPES ) {
				pixel_type = c.channel().type;
			}
			if ( pixel_type != c.channel().type ) {
				assert( false && "Pixel type is not consistent over all channels" );
				delete self->inputFile;
				delete self;
				return nullptr;
			}

			if ( c.name() ) {
				channel_names[ self->channel_count ] = c.name()[ 0 ];
			}

			self->channel_count++;
		}

		if ( self->channel_count == 1 ) {

			switch ( pixel_type ) {
			case ( Imf::PixelType::FLOAT ):
				self->image_inferred_format = le::Format::eR32Sfloat;
				break;
			case ( Imf::PixelType::HALF ):
				self->image_inferred_format = le::Format::eR16Sfloat;
				break;
			case ( Imf::PixelType::UINT ):
				self->image_inferred_format = le::Format::eR32Uint;
				break;
			default:
				assert( false );
			}
		} else if ( self->channel_count == 3 ) {

			switch ( pixel_type ) {
			case ( Imf::PixelType::FLOAT ):
				self->image_inferred_format = le::Format::eR32G32B32Sfloat;
				break;
			case ( Imf::PixelType::HALF ):
				self->image_inferred_format = le::Format::eR16G16B16Sfloat;
				break;
			case ( Imf::PixelType::UINT ):
				self->image_inferred_format = le::Format::eR32G32B32Uint;
				break;
			default:
				assert( false );
			}
		} else if ( self->channel_count == 4 ) {

			switch ( pixel_type ) {
			case ( Imf::PixelType::FLOAT ):
				self->image_inferred_format = le::Format::eR32G32B32A32Sfloat;
				break;
			case ( Imf::PixelType::HALF ):
				self->image_inferred_format = le::Format::eR16G16B16A16Sfloat;
				break;
			case ( Imf::PixelType::UINT ):
				self->image_inferred_format = le::Format::eR32G32B32A32Uint;
				break;
			default:
				assert( false );
			}
		} else {
			logger.warn( "Could not infer image format for image: %s", file_path );
			self->image_inferred_format = le::Format::eR32G32B32A32Sfloat;
		}

	} catch ( const std::exception& e ) {
		delete self->inputFile;
		delete self;
		logger.error( "Error reading image file: %s", std::string( e.what() ).c_str() );
		return nullptr;
	}

	return self;
}

// ----------------------------------------------------------------------

static void le_image_decoder_destroy( le_image_decoder_o* self ) {
	delete self->inputFile;
	delete self;
}

// ----------------------------------------------------------------------

static bool le_image_decoder_read_pixels( le_image_decoder_o* self, uint8_t* pixel_data, size_t pixels_byte_count ) {

	Imath::Box2i dw = self->inputFile->header().dataWindow();

	int dw_x = dw.min.x;
	int dw_y = dw.min.y;

	int w = self->image_width;
	int h = self->image_height;

	Imf::FrameBuffer framebuffer;

	// Create a framebuffer layout for requested format eR32G32B32A32Sfloat -
	// The framebuffer is essentially a descriptor for the data held in `pixel_data`
	//

	le_num_type num_type     = {};
	uint32_t    num_requested_channels = {}; // number of channels requested by the output image format

	le_format_infer_channels_and_num_type( self->getFormat(), &num_requested_channels, &num_type );

	uint32_t bytes_per_pixel = size_of( num_type ) * num_requested_channels;
	size_t   num_data_bytes  = w * h * bytes_per_pixel;

	uint32_t offsets_per_channel[ 4 ] = {
	    size_of( num_type ) * 0,
	    size_of( num_type ) * 1,
	    size_of( num_type ) * 2,
	    size_of( num_type ) * 3,
	};

	// In case we only have a single channel, we must name it Y for luminance
	const std::string names_for_channels = num_requested_channels > 1 ? "RGBA" : "Y";

	for ( int i = 0; i != num_requested_channels; i++ ) {

		Imf::PixelType pixel_type = Imf::PixelType::NUM_PIXELTYPES;

		switch ( num_type ) {
		case ( le_num_type::eU32 ):
			pixel_type = Imf::PixelType::UINT;
			break;
		case ( le_num_type::eF32 ):
			pixel_type = Imf::PixelType::FLOAT;
			break;
		case ( le_num_type::eF16 ):
			pixel_type = Imf::PixelType::HALF;
			break;
		default:
			logger.error( "Invalid format as target for EXR image." );
			return false;
		}

		framebuffer.insert(
		    { names_for_channels[ i ] },
		    Imf::Slice(
		        pixel_type,                                                                              // pixel data type
		        ( char* )pixel_data + ( -dw_y * w - dw_x ) * bytes_per_pixel + offsets_per_channel[ i ], // base offset
		        bytes_per_pixel,                                                                         // x-stride
		        bytes_per_pixel * w,                                                                     // y-stride
		        1,                                                                                       // x-sampling
		        1,                                                                                       // y-sampling
		        1.0                                                                                      // fill value (default value)
		        )                                                                                        //
		);
	}

	self->inputFile->setFrameBuffer( framebuffer );

	// Pixels now contains all the data in the correct layout.
	// you can access the data via the &pixels[0][0].b;

	if ( w * h * bytes_per_pixel <= pixels_byte_count ) {
		self->inputFile->readPixels( dw.min.y, dw.max.y );
		logger.info( "Successfully read image into pixels buffer." );
		return true;
	} else {
		logger.error( "Could not read pixels." );

		return false;
	}
}

// ----------------------------------------------------------------------

static void le_image_decoder_get_image_data_description( le_image_decoder_o* self, le_image_decoder_format_o* p_format, uint32_t* w, uint32_t* h ) {

	assert( self );

	auto fmt = self->getFormat();

	if ( p_format ) {
		p_format->format = fmt;
	}
	if ( w ) {
		*w = self->image_width;
	}
	if ( h ) {
		*h = self->image_height;
	}
};

// ----------------------------------------------------------------------

static void le_image_decoder_set_requested_format( le_image_decoder_o* self, le_image_decoder_format_o const* format ) {
	self->image_requested_format = format->format;
}

// ----------------------------------------------------------------------

void le_register_exr_decoder_api( void* api ) {

	auto& le_image_decoder_i = static_cast<le_exr_api*>( api )->le_exr_image_decoder_i;

	if ( le_image_decoder_i == nullptr ) {
		le_image_decoder_i = new le_image_decoder_interface_t{};
	} else {
		// The interface already existed - we have been reloaded and only just need to update
		// function pointer addresses.
		//
		// This is important as by not re-allocating a new interface object
		// but by updating the existing interface object by-value, we keep the *public
		// address for the interface*, while updating its function pointers.
		*le_image_decoder_i = le_image_decoder_interface_t();
	}

	le_image_decoder_i->create_image_decoder       = le_image_decoder_create;
	le_image_decoder_i->destroy_image_decoder      = le_image_decoder_destroy;
	le_image_decoder_i->read_pixels                = le_image_decoder_read_pixels;
	le_image_decoder_i->get_image_data_description = le_image_decoder_get_image_data_description;
	le_image_decoder_i->set_requested_format       = le_image_decoder_set_requested_format;
}
