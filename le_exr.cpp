#include "le_exr.h"

// TODO: You must find the correct name for the dynamic library to include
// under windows.
#if ( WIN32 ) and defined( PLUGINS_DYNAMIC )
#	pragma comment( lib, "bin/modules/OpenEXR-3_3.lib" )
#endif

// ----------------------------------------------------------------------
extern void le_register_exr_decoder_api( void* ); // in le_exr_decoder.cpp
extern void le_register_exr_encoder_api( void* ); // in le_exr_encoder.cpp

LE_MODULE_REGISTER_IMPL( le_exr, api ) {

	le_register_exr_decoder_api( api );
	le_register_exr_encoder_api( api );

#if defined PLUGINS_DYNAMIC
	le_core_load_library_persistently( "./modules/libOpenEXR-3_3.so" );
#endif
}
