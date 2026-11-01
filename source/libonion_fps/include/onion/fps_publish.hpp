/* Copyright (C) 2026 OnionHEN / LightningMods */
#pragma once

#include <onion/fps_sample.h>

namespace onion {
namespace fps {

bool publish_open();
void publish(const OnionFpsSample &sample);

} // namespace fps
} // namespace onion
