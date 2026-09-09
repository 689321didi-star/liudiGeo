#pragma once

#include "wave3d/io/data.hpp"

#include <string>

namespace wave3d::io {

[[nodiscard]] ForwardRunConfiguration load_yaml_run_configuration(
    const std::string& path);

[[nodiscard]] std::string resolved_yaml(
    const ForwardRunConfiguration& configuration);

void write_resolved_yaml(
    const std::string& path,
    const ForwardRunConfiguration& configuration);

} // namespace wave3d::io
