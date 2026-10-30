#ifndef VIDEO_H264_H
#define VIDEO_H264_H
#include "video.h"

/* Décodage des vidéos .h264 (flux brut Baseline, voir h264idx.h). Même contrat que video_open/seek/update/close ;
   appelé par video.c, jamais directement. Deux implémentations : le décodeur matériel de la Vita (sceAvcdec,
   video_h264_vita.c) et h264bsd en logiciel partout ailleurs (video_h264_pc.c). */
bool video_h264_open(Video *v, SDL_Renderer *r, const char *path);
void video_h264_close(Video *v);
void video_h264_seek(Video *v, float t_ms);
void video_h264_update(Video *v);

#endif
