// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_CONVERSION_PIPELINE_BUILDER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_CONVERSION_PIPELINE_BUILDER_H_

#include <lib/fit/function.h>

#include "src/media/playback/mediaplayer/decode/decoder.h"
#include "src/media/playback/mediaplayer/graph/graph.h"
#include "src/media/playback/mediaplayer/graph/packet.h"
#include "src/media/playback/mediaplayer/graph/types/stream_type.h"

namespace media_player {

// Attempts to add transforms to the given pipeline to convert in_type to a
// type compatible with out_type_sets. If it succeeds, the function calls back
// with the OutputRef for the end of the pipeline and the new stream type. If it
// fails, the function calls back with the original OutputRef and a null
// stream type pointer.
void BuildConversionPipeline(
    const StreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, DecoderFactory* decoder_factory, OutputRef output,
    fit::function<void(OutputRef, std::unique_ptr<StreamType>)> callback);

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_CONVERSION_PIPELINE_BUILDER_H_
