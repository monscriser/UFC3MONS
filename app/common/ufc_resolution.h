#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace ufc {
struct Resolution {
 int width, height, scale;
};
inline Resolution SelectedResolution(int selection) {
 constexpr Resolution modes[]={{1280,720,1},{1920,1080,2},{2560,1440,2},{3840,2160,3}};
 return modes[std::clamp(selection,0,3)];
}
inline void AppendResolutionArguments(std::vector<std::string>& args,int selection) {
 const auto mode=SelectedResolution(selection);
 // SDK raster scaling is integer-only. 1080p uses 2x and is downsampled.
 // Keep the emulated display at 720p/60 Hz: output must not alter simulation.
 for(auto value:{"--video_mode_width=1280","--video_mode_height=720","--video_mode_refresh_rate=60"})args.emplace_back(value);
 // Set shared and per-axis values, including 1x, to override saved SDK cvars.
 args.push_back("--resolution_scale="+std::to_string(mode.scale));
 args.push_back("--draw_resolution_scale_x="+std::to_string(mode.scale));
 args.push_back("--draw_resolution_scale_y="+std::to_string(mode.scale));
 args.push_back("--window_width="+std::to_string(mode.width));
 args.push_back("--window_height="+std::to_string(mode.height));
}
}
