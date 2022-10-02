
#include "piclimiter.h"


typedef float float32_t;


#if WIN32
uint16_t sys_get_time( )
{
	return ( uint16_t ) GetTickCount( );
}

void sys_sleep_1ms( )
{
	Sleep( 1 );
}

#else
#error you need to implement a function called sys_get_time() which returns a uint16_t with the lowest 16 bits of absolute milliseconds passed since a fixed point
#error you also need to implement a function called sys_sleep_1ms which sleeps for around 1ms
#endif


void piclimiter_wait_for_next( piclimiter_t *ps_limiter )
{
	int64_t i64_don_duration_exact, i64_don_duration_jitter, i64_don_duration_jitter_ms, i64_don_duration_jitter_timescale;
	int64_t i64_delta_exact_jitter;
	int32_t i_next_ms;
	bool b_starving = true;

	if( ps_limiter->i_don == 0 )
	{
		ps_limiter->ui16_last_pic_ms = sys_get_time( );
	}

	i64_delta_exact_jitter = ps_limiter->i64_last_pic_duration_jitter;
	i64_don_duration_exact = ps_limiter->i_picture_duration;

	i64_don_duration_jitter = i64_don_duration_exact + i64_delta_exact_jitter;
	i64_don_duration_jitter_ms = ( ( i64_don_duration_jitter * 1000 ) / ps_limiter->i_timescale );
	i64_don_duration_jitter_timescale = ( i64_don_duration_jitter_ms * ps_limiter->i_timescale ) / 1000;

	i64_delta_exact_jitter = i64_delta_exact_jitter - i64_don_duration_jitter_timescale + ps_limiter->i_picture_duration;

	ps_limiter->i64_last_pic_duration_jitter = i64_delta_exact_jitter;

	i_next_ms = ( int32_t )( ps_limiter->ui16_last_pic_ms + i64_don_duration_jitter_ms );

	while( 1 )
	{
		int32_t i_time_ms;

		i_time_ms = ( sys_get_time( ) - ps_limiter->ui16_last_pic_ms );
		while( i_time_ms < 0 )
		{
			i_time_ms += ( 1 << 16 );
		}
		i_time_ms += ps_limiter->ui16_last_pic_ms;

		if( i_time_ms >= i_next_ms )
		{
			break;
		}
		sys_sleep_1ms( );
		b_starving = false;
	}

	if( b_starving )
	{
		fprintf( stderr, "WARNING: DON %d -> No Wait, Not enough input ?\n", ps_limiter->i_don );
	}

	ps_limiter->ui16_last_pic_ms = i_next_ms & 0xffff;
}



int32_t piclimiter_find_aud( piclimiter_t *ps_limiter, int32_t i_offset )
{
	int32_t i_idx, i_zero_count;

	i_zero_count = 0;
	for( i_idx = i_offset; i_idx < ps_limiter->i_buffer_fill_bytes; i_idx++ )
	{
		if( ps_limiter->rgui8_buffer[ i_idx ] == 0 )
		{
			i_zero_count++;
		}
		else
		{
			if( i_zero_count >= 2 && ps_limiter->rgui8_buffer[ i_idx ] == 1 && ( i_idx + 3 ) < ps_limiter->i_buffer_fill_bytes )
			{
				uint32_t ui_nalu_hdr;
				int32_t i_check, i_nalu_type, i_nuh_layer_id, i_temporal_id;

				ui_nalu_hdr = ( ps_limiter->rgui8_buffer[ i_idx + 1 ] << 8 ) | ps_limiter->rgui8_buffer[ i_idx + 2 ];

				/* nalu start, check for aud */
				i_check = ( ui_nalu_hdr >> 15 ) & 0x1;
				i_nalu_type = ( ui_nalu_hdr >> 9 ) & 0x3f;
				i_nuh_layer_id = ( ui_nalu_hdr >> 3 ) & 0x3f;
				i_temporal_id = ( ui_nalu_hdr & 0x7 );

				if( ps_limiter->i_verbose >= 2 )
				{
					fprintf( stderr, "nalu? c: %d, t: %d, l: %d, i: %d\n", i_check, i_nalu_type, i_nuh_layer_id, i_temporal_id );
				}

				if( i_nalu_type == 35 )
				{
					return ( i_idx - ( i_zero_count < 2 ? 2 : 2 ) ); /* dont commit trailing zeroes from prev */
				}
			}

			i_zero_count = 0;
		}
	}
	return -1;
}


void piclimiter_advance_hevc( piclimiter_t *ps_limiter, int32_t i_advance, bool b_is_picture )
{
	int32_t i_idx;
	int64_t i64_size;
	float32_t f32_seconds, f32_bits;


	piclimiter_wait_for_next( ps_limiter );

	fwrite( &ps_limiter->rgui8_buffer[ 0 ], sizeof( uint8_t ), i_advance, ps_limiter->pf_out );

	memmove( &ps_limiter->rgui8_buffer[ 0 ], &ps_limiter->rgui8_buffer[ i_advance ], ( ps_limiter->i_buffer_fill_bytes - i_advance ) * sizeof( uint8_t ) );
	ps_limiter->i_buffer_fill_bytes -= i_advance;


	if( b_is_picture )
	{
		if( ps_limiter->i_current_picture_history_count == ps_limiter->i_target_picture_history_count )
		{
			for( i_idx = 1; i_idx < ps_limiter->i_target_picture_history_count; i_idx++ )
			{
				ps_limiter->rgi_picture_history_size[ i_idx - 1 ] = ps_limiter->rgi_picture_history_size[ i_idx ];
			}
			ps_limiter->rgi_picture_history_size[ ps_limiter->i_target_picture_history_count - 1 ] = i_advance;
		}
		else
		{
			ps_limiter->rgi_picture_history_size[ ps_limiter->i_current_picture_history_count++ ] = i_advance;
		}

		ps_limiter->i_don++;

		if( ( ( ps_limiter->i_don + 1 ) % ps_limiter->i_target_picture_history_count ) == 0 )
		{
			i64_size = 0;
			for( i_idx = 0; i_idx < ps_limiter->i_current_picture_history_count; i_idx++ )
			{
				i64_size += ps_limiter->rgi_picture_history_size[ i_idx ];
			}
			f32_bits = ( i64_size * 8.0f );
			f32_seconds = ( ( ( float32_t ) ps_limiter->i_current_picture_history_count ) * ps_limiter->i_picture_duration ) / ps_limiter->i_timescale;
			if( ps_limiter->i_verbose >= 1 )
			{
				fprintf( stderr, "Limiter -> Don %d, Rate: %.2f\n", ps_limiter->i_don, ( f32_bits / f32_seconds ) / 1000.0f );
			}
		}
	}
}

bool piclimiter_process_hevc( piclimiter_t *ps_limiter )
{
	bool b_eos;
	int32_t i_advance;
	size_t i_read;

	i_read = fread( &ps_limiter->rgui8_buffer[ ps_limiter->i_buffer_fill_bytes ], sizeof( uint8_t ), ps_limiter->i_buffer_size_bytes - ps_limiter->i_buffer_fill_bytes, ps_limiter->pf_in );
	if( i_read > 0 )
	{
		ps_limiter->i_buffer_fill_bytes += ( int32_t )i_read;
	}

	b_eos = false;
	if( i_read == 0 && ps_limiter->i_buffer_fill_bytes < ps_limiter->i_buffer_size_bytes )
	{
		b_eos = true;
	}

	if( ps_limiter->i_buffer_fill_bytes == 0 )
	{
		return false;
	}

	if( !ps_limiter->b_synced )
	{
		i_advance = piclimiter_find_aud( ps_limiter, 0 );
		if( i_advance < 0 )
		{
			fprintf( stderr, "ERROR: cannot find access unit delimiter, bitstream error ?\n" );
			exit( 1 );
		}
		piclimiter_advance_hevc( ps_limiter, i_advance, false );
		ps_limiter->b_synced = true;
	}

	if( ps_limiter->b_synced )
	{
		i_advance = piclimiter_find_aud( ps_limiter, 3 );
		if( i_advance > 0 )
		{
			piclimiter_advance_hevc( ps_limiter, i_advance, true );
			return true;
		}
		else if( b_eos && ps_limiter->i_buffer_fill_bytes > 0 )
		{
			/* last one ? */
			piclimiter_advance_hevc( ps_limiter, ps_limiter->i_buffer_fill_bytes, true );
			return false;
		}
	}

	return false;
}


void piclimiter_advance_yuv( piclimiter_t *ps_limiter )
{
	int32_t i_idx;
	uint8_t *pui8_advance;

	piclimiter_wait_for_next( ps_limiter );

	fwrite( ps_limiter->rgpui8_picturebuffer[ 0 ], sizeof( uint8_t ) * ps_limiter->i_picture_buffer_size, 1, ps_limiter->pf_out );

	pui8_advance = ps_limiter->rgpui8_picturebuffer[ 0 ];
	for( i_idx = 0; i_idx < ps_limiter->i_buffer_size_pictures - 1; i_idx++ )
	{
		ps_limiter->rgpui8_picturebuffer[ i_idx ] = ps_limiter->rgpui8_picturebuffer[ i_idx + 1 ];
	}
	ps_limiter->rgpui8_picturebuffer[ i_idx ] = pui8_advance;

	ps_limiter->i_don++;

	if( ( ( ps_limiter->i_don + 1 ) % ps_limiter->i_target_picture_history_count ) == 0 )
	{
		if( ps_limiter->i_verbose >= 1 )
		{
			fprintf( stderr, "Limiter -> Don %d\n", ps_limiter->i_don );
		}
	}

	ps_limiter->i_buffer_fill_pictures--;

}


bool piclimiter_process_yuv( piclimiter_t *ps_limiter )
{
	bool b_eos;
	size_t i_read;

	while( ps_limiter->i_buffer_fill_pictures < ps_limiter->i_buffer_size_pictures )
	{
		i_read = fread( ps_limiter->rgpui8_picturebuffer[ ps_limiter->i_buffer_fill_pictures ], sizeof( uint8_t ) * ps_limiter->i_picture_buffer_size, 1, ps_limiter->pf_in );
		if( i_read > 0 )
		{
			ps_limiter->i_buffer_fill_pictures++;
		}
		else
		{
			break;
		}
	}

	b_eos = false;
	if( i_read == 0 && ps_limiter->i_buffer_fill_pictures < ps_limiter->i_buffer_size_pictures )
	{
		b_eos = true;
	}

	if( ps_limiter->i_buffer_fill_pictures == 0 )
	{
		return false;
	}

	piclimiter_advance_yuv( ps_limiter );

	return true;
}


bool piclimiter_process( piclimiter_t *ps_limiter )
{
	if( ps_limiter->i_mode == PICLIMITER_MODE_HEVC )
	{
		return piclimiter_process_hevc( ps_limiter );
	}
	else if( ps_limiter->i_mode == PICLIMITER_MODE_YUV )
	{
		return piclimiter_process_yuv( ps_limiter );
	}
	return false;
}

void usage( )
{
	fprintf( stderr, "piclimiter options ( * are mandatory ):\n" );
	fprintf( stderr, "-in <file>            | * input file, '-' for stdin\n" );
	fprintf( stderr, "-out <file>           | * output file, '-' for stdout\n" );
	fprintf( stderr, "-mode <mode>          | * operation mode, 'yuv' or 'hevc'\n" );
	fprintf( stderr, "-timescale <int>      | timescale\n" );
	fprintf( stderr, "-picduration <int>    | picture duration\n" );
	fprintf( stderr, "-buffer_pics <int>    | picture buffer size for yuv mode\n" );
	fprintf( stderr, "-pic_buffer_size      | size of a yuv picture in bytes for yuv mode.\n" );
	fprintf( stderr, "                      | yuv420p would be ( ( width * height * 3 ) / 2 )\n" );
	fprintf( stderr, "-buffer_kbit <int>    | video buffer size for hevc mode\n" );
	fprintf( stderr, "-verbose <int>        | (0-2)\n" );
}

int32_t main( int32_t i_argc, char *rgpc_argv[] )
{
	FILE *pf_in, *pf_out;
	int32_t i_mode;
	int32_t i_idx, i_timescale, i_picture_duration, i_buffer_kbit, i_buffer_pictures, i_picture_buffer_size, i_verbose;
	piclimiter_t *ps_limiter;
	
	/* defaults */
	i_mode = 0;
	pf_in = NULL;
	pf_out = NULL;
	i_timescale = 25;
	i_picture_duration = 1;
	i_buffer_kbit = 2000;
	i_buffer_pictures = 5;
	i_picture_buffer_size = ( 352 * 288 * 3 ) / 2;
	i_verbose = 2;
	
	if( i_argc < 3 )
	{
		usage();
	}

	i_idx = 1;
	while( i_idx != i_argc )
	{
		if( strcmp( rgpc_argv[ i_idx ], "-in" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_idx++;
			if( strcmp( rgpc_argv[ i_idx ], "-" ) == 0 )
			{
#ifdef WIN32
				_setmode( _fileno( stdin ), _O_BINARY );
#else
				stdin = freopen( NULL, "rb", stdin );
#endif
				pf_in = stdin;
			}
			else
			{
				pf_in = fopen( rgpc_argv[ i_idx ], "rb" );
				if( !pf_in )
				{
					fprintf( stderr, "ERROR: cannot open file '%s' for input\n", rgpc_argv[ i_idx ] );
					exit( -1 );
				}
			}
		}
		else if( strcmp( rgpc_argv[ i_idx ], "-out" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_idx++;
			if( strcmp( rgpc_argv[ i_idx ], "-" ) == 0 )
			{
#ifdef WIN32
				_setmode( _fileno( stdout ), _O_BINARY );
#else
				stdin = freopen( NULL, "rb", stdin );
#endif
				pf_out = stdout;
			}
			else
			{
				pf_out = fopen( rgpc_argv[ i_idx ], "wb" );
				if( !pf_out )
				{
					fprintf( stderr, "ERROR: cannot open file '%s' for output\n", rgpc_argv[ i_idx ] );
					exit( -1 );
				}
			}
		}
		else if( strcmp( rgpc_argv[ i_idx ], "-mode" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_idx++;
			if( strcmp( rgpc_argv[ i_idx ], "yuv" ) == 0 )
			{
				i_mode = PICLIMITER_MODE_YUV;
			}
			else if( strcmp( rgpc_argv[ i_idx ], "hevc" ) == 0 )
			{
				i_mode = PICLIMITER_MODE_HEVC;
			}
			else
			{
				fprintf( stderr, "ERROR: invalid argument '%s' for -mode\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
		}
		else if( strcmp( rgpc_argv[ i_idx ], "-timescale" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_timescale = atoi( rgpc_argv[ ++i_idx ] );
		}
		else if( strcmp( rgpc_argv[ i_idx ], "-picduration" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_picture_duration = atoi( rgpc_argv[ ++i_idx ] );
		}
		else if( strcmp( rgpc_argv[ i_idx ], "-pic_buffer_size" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_picture_buffer_size = atoi( rgpc_argv[ ++i_idx ] );
		}
		else if( strcmp( rgpc_argv[ i_idx ], "-buffer_pics" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_buffer_pictures = atoi( rgpc_argv[ ++i_idx ] );
		}
		else if( strcmp( rgpc_argv[ i_idx ], "-buffer_kbit" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_buffer_kbit = atoi( rgpc_argv[ ++i_idx ] );
		}
		else if( strcmp( rgpc_argv[ i_idx ], "-verbose" ) == 0 )
		{
			if( i_argc <= i_idx + 1 )
			{
				fprintf( stderr, "ERROR: parameter %s requires an argument\n", rgpc_argv[ i_idx ] );
				exit( -1 );
			}
			i_verbose = atoi( rgpc_argv[ ++i_idx ] );
		}
		else
		{
			fprintf( stderr, "ERROR: unknown command line argument '%s', try calling without args\n", rgpc_argv[ i_idx ] );
			exit( -1 );
		}
		i_idx++;
	}

	if( !pf_in )
	{
		fprintf( stderr, "ERROR: no input file\n" );
		exit( -1 );
	}
	if( !pf_out )
	{
		fprintf( stderr, "ERROR: no output file\n" );
		exit( -1 );
	}

	if( i_mode != PICLIMITER_MODE_HEVC &&
		i_mode != PICLIMITER_MODE_YUV )
	{
		fprintf( stderr, "ERROR: operation -mode not specified\n" );
		exit( -1 );
	}

	fprintf( stderr, "picture limiting to ts: %d, dura: %d ( ~%.4ffps)\n", i_timescale, i_picture_duration, ( ( float32_t )i_timescale ) / i_picture_duration );

	if( i_mode == PICLIMITER_MODE_HEVC )
	{
		fprintf( stderr, "mode: HEVC, need AUD in stream\n" );
		fprintf( stderr, "buffer size: %d kbit\n", i_buffer_kbit );
	}
	else
	{
		fprintf( stderr, "mode: YUV, need correct picture buffer size\n" );
		fprintf( stderr, "picture buffer count: %d of %d bytes\n", i_buffer_pictures, i_picture_buffer_size );
	}

	ps_limiter = malloc( sizeof( piclimiter_t ) );
	memset( ps_limiter, 0, sizeof( piclimiter_t ) );

	/* init */
	ps_limiter->pf_in = pf_in;
	ps_limiter->pf_out = pf_out;
	ps_limiter->i_timescale = i_timescale;
	ps_limiter->i_picture_duration = i_picture_duration;
	ps_limiter->i64_video_buffer_size_bits = ( ( ( int64_t ) i_buffer_kbit ) * 1000 );
	ps_limiter->i_verbose = i_verbose;
	ps_limiter->i_mode = i_mode;

	ps_limiter->i_target_picture_history_count = ( ps_limiter->i_timescale + ps_limiter->i_picture_duration - 1 ) / ps_limiter->i_picture_duration;


	if( i_mode == PICLIMITER_MODE_HEVC )
	{
		ps_limiter->i_buffer_size_bytes = ( int32_t ) ( ps_limiter->i64_video_buffer_size_bits / 8 );
		if( ps_limiter->i_buffer_size_bytes > PICLIMIER_MAX_BUFFER_SIZE )
		{
			fprintf( stderr, "ERROR: buffer size of %d bytes is too large\n", ps_limiter->i_buffer_size_bytes );
			exit( -1 );
		}
	}
	else
	{
		ps_limiter->i_buffer_size_pictures = i_buffer_pictures;
		ps_limiter->i_picture_buffer_size = i_picture_buffer_size;
		if( ps_limiter->i_buffer_size_pictures > PICLIMITER_MAX_PICTURE_BUFFER_SIZE )
		{
			fprintf( stderr, "ERROR: picture buffer count of %d is too large (max %d)\n", ps_limiter->i_buffer_size_pictures, PICLIMITER_MAX_PICTURE_BUFFER_SIZE );
			exit( -1 );
		}
		for( i_idx = 0; i_idx < ps_limiter->i_buffer_size_pictures; i_idx++ )
		{
			ps_limiter->rgpui8_picturebuffer[ i_idx ] = malloc( ps_limiter->i_picture_buffer_size * sizeof( uint8_t ) );
		}
	}

	while( 1 )
	{
		if( !piclimiter_process( ps_limiter ) )
		{
			break;
		}
	}


	fprintf( stderr, "end of stream. maybe...\n");

}











