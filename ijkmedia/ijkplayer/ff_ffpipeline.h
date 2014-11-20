/*
 * ff_ffpipeline.h
 *
 * Copyright (c) 2014 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef FFPLAY__FF_FFPIPELINE_H
#define FFPLAY__FF_FFPIPELINE_H

#include "ijksdl/ijksdl_class.h"
#include "ijksdl/ijksdl_mutex.h"
#include "ff_ffpipenode.h"

typedef struct IJKFF_Pipeline_Opaque IJKFF_Pipeline_Opaque;
typedef struct IJKFF_Pipeline IJKFF_Pipeline;
typedef struct IJKFF_Pipeline {
    IJKSDL_Class *opaque_class;
    void         *opaque;

    void            (*func_destroy)            (IJKFF_Pipeline *pipeline);
    IJKFF_Pipenode *(*func_open_video_decoder) (IJKFF_Pipeline *pipeline, FFPlayer *ffp);
    IJKFF_Pipenode *(*func_open_video_output)  (IJKFF_Pipeline *pipeline, FFPlayer *ffp);
} IJKFF_Pipeline;

IJKFF_Pipeline *ffpipeline_alloc(IJKSDL_Class *opaque_class, size_t opaque_size);
void ffpipeline_free(IJKFF_Pipeline *pipeline);
void ffpipeline_free_p(IJKFF_Pipeline **pipeline);

IJKFF_Pipenode* ffpipeline_open_video_decoder(IJKFF_Pipeline *pipeline, FFPlayer *ffp);
IJKFF_Pipenode* ffpipeline_open_video_output(IJKFF_Pipeline *pipeline, FFPlayer *ffp);

#endif
