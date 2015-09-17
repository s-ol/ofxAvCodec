//
//  ofxAvMetadata.cpp
//  emptyExample
//
//  Created by Hansi on 16.09.15.
//
//

#include "ofxAvMetadata.h"
#include "ofxAvAudioPlayer.h"
#include "ofMain.h"

using namespace std;

std::map<string,string> ofxAvMetadata::read( std::string filename ){
	string fileNameAbs = ofToDataPath(filename,true);
	const char * input_filename = fileNameAbs.c_str();
	// the first finds the right codec, following  https://blinkingblip.wordpress.com/2011/10/08/decoding-and-playing-an-audio-stream-using-libavcodec-libavformat-and-libao/
	
	AVFormatContext * container = 0;
	if (avformat_open_input(&container, input_filename, NULL, NULL) < 0) {
		cerr << "couldn't open the file :(" << endl;
		return map<string,string>();
	}
	
	map<string,string> meta;
	AVDictionary * d = container->metadata;
	AVDictionaryEntry *t = NULL;
	while ((t = av_dict_get(d, "", t, AV_DICT_IGNORE_SUFFIX))!=0){
		meta[string(t->key)] = string(t->value);
	}
	
	avformat_close_input(&container);
	avformat_free_context(container);
	
	return meta;
}


bool ofxAvMetadata::update(std::string filename, std::map<std::string, std::string> newMetadata){
	string fileNameAbs = ofToDataPath(filename,true);
	const char * input_filename = fileNameAbs.c_str();
	AVCodecContext * codec_context = NULL;
	AVCodec* codec = NULL;
	AVFormatContext *ofmt_ctx = NULL;
	AVCodec * out_codec = NULL;
	AVStream * out_stream = NULL;
	AVCodecContext * out_c = NULL;
	int i, ret;
	int audio_stream_id = -1;
	AVDictionary *d = NULL;
	AVPacket * packet = NULL;
	AVFormatContext* container = NULL;
	map<string,string>::iterator it;
	bool success = false;
	Poco::Path filepath(fileNameAbs);
	filepath.setBaseName("." + filepath.getBaseName()+".meta_tmp");
	string destFile = filepath.toString();
	
	
	if (avformat_open_input(&container, input_filename, NULL, NULL) < 0) {
		cerr << ("Could not open file") << endl;
		goto panic;
	}
 
	if (avformat_find_stream_info(container,NULL) < 0) {
		cerr << ("Could not find file info") << endl;
		goto panic;
	}
 
	audio_stream_id = -1;
 
	// To find the first audio stream. This process may not be necessary
	// if you can gurarantee that the container contains only the desired
	// audio stream
	for (i = 0; i < container->nb_streams; i++) {
		if (container->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream_id = i;
			break;
		}
	}
 
	if (audio_stream_id == -1) {
		cerr << ("Could not find an audio stream") << endl;
		goto panic;
	}
 
	// Find the apropriate codec and open it
	codec_context = container->streams[audio_stream_id]->codec;
 
	codec = avcodec_find_decoder(codec_context->codec_id);
	if (avcodec_open2(codec_context, codec,NULL)) {
		cerr << ("Could not find open the needed codec") << endl;
		goto panic;
	}
	
	// ---------------------------------------------
	// OK OK OK
	// WE HAVE AN OPEN FILE!!!!
	// now let's create the output file ...
	// ---------------------------------------------
	
	avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, destFile.c_str());
	if (!ofmt_ctx) {
		av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
		goto panic;
	}
	
	out_codec = avcodec_find_encoder(codec->id);
	if (!out_codec) {
		cerr << ("codec not found") << endl;
		return false;
	}
	
	out_stream = avformat_new_stream(ofmt_ctx, out_codec);
	if (!out_stream) {
		cerr << ("Failed allocating output stream\n") << endl;
		return false;
	}
	
	out_c = out_stream->codec;
	if (!out_c) {
		cerr << ("could not allocate audio codec context") << endl;
		return false;
	}
	
	out_c->sample_fmt = codec_context->sample_fmt;
	out_c->sample_rate = codec_context->sample_rate;
	out_c->channel_layout = codec_context->channel_layout;
	out_c->channels       = codec_context->channels;
	out_c->frame_size = codec_context->frame_size;
	
	if (avcodec_open2(out_c, out_codec, NULL) < 0) {
		cerr << ("could not open codec") << endl;
		goto panic;
	}
	
	
	
	if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		int ret = avio_open(&ofmt_ctx->pb, destFile.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			cerr << ("Could not open output file") << endl;
			goto panic;
		}
	}
	
	it = newMetadata.begin();
	while( it != newMetadata.end() ){
		av_dict_set(&d, (*it).first.c_str(), (*it).second.c_str(), 0);
		++it;
	}
	
	ofmt_ctx->metadata = d;
	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
		goto panic;
	}
	
	
	
	// ---------------------------------------------
	// OK, We have all we need, really. let's start this party!
	// ---------------------------------------------
	
	//packet.data = inbuf;
	//packet.size = fread(inbuf, 1, AVCODEC_AUDIO_INBUF_SIZE, f);
	packet = new AVPacket();
	av_init_packet(packet);
	packet->data = NULL;
	packet->size = 0;
	
	// read all packet (frames? who the fuck knows)
	while(true){
		int res = av_read_frame(container, packet);
		if( res >= 0 ){
			av_write_frame(ofmt_ctx, packet);
		}
		else{
			break;
		}
	}
	
	av_write_trailer(ofmt_ctx);
	
	success = true;
	
panic:
	if( packet ){
		av_free_packet(packet);
	}
	
	if( ofmt_ctx ){
		// also closes out_c and out_stream and out_codec! ?
		avformat_free_context(ofmt_ctx);
	}
	
	if( codec_context ){
		avcodec_close(codec_context);
		av_free(codec_context);
	}
	
	if( success ){
		remove(fileNameAbs.c_str());
		rename(destFile.c_str(), fileNameAbs.c_str());
	}
	else{
		remove(destFile.c_str());
	}
	
	return success;
}

double ofxAvMetadata::duration( std::string filename ){
	AVFormatContext* pFormatCtx = avformat_alloc_context();
	string file = ofToDataPath(filename);
	avformat_open_input(&pFormatCtx, file.c_str(), NULL, NULL);
	avformat_find_stream_info(pFormatCtx,NULL);
	int64_t duration = pFormatCtx->duration;
	avformat_close_input(&pFormatCtx);
	avformat_free_context(pFormatCtx);
	
	return duration/(double)AV_TIME_BASE;
}

float * ofxAvMetadata::amplitudePreview( std::string filename, int width ){
	float * result = new float[width];
	memset(result, 0, width*sizeof(float));
	double duration = ofxAvMetadata::duration(filename);
	if( duration == 0 ) return result;
	
	ofxAvAudioPlayer player;
	player.setupAudioOut(1, (int)(100*width/duration));
	player.loadSound(filename);
	float buffer[100];
	for( int i = 0; i < width; i++ ){
		memset(buffer, 0, 100*sizeof(float)); 
		player.audioOut(buffer, 100, 1);
		// sorry!
		float max = 0;
		for( int j = 0; j < 100; j++ ){
			max = MAX(abs(buffer[j]),max);
		}
		result[i] = max;
	}
	return result;
}
