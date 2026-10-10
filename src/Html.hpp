#pragma once

#include "Model.hpp"

#include <functional>
#include <string>

// The logs as one HTML page to share: groups fold like on the screen, levels in color,
// a box at the top filters the lines. Everything is in the file, nothing to download
namespace html
{
  struct Options
  {
    std::string title;
    // Which groups and rows go in, like the screen's filters
    std::function<bool(model::Group const&)> keepGroup;
    std::function<bool(Record const&)> keepRow;
  };

  std::string render(model::Model const& model, Options const& options);

  // "<", ">", "&" and quotes, for text inside the page
  std::string escape(std::string_view text);

  // The console's 256 colors as "#rrggbb"
  std::string color(int index);
}
