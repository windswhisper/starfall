#pragma once
#include <Surface.h>
#include <Memory.h>
#include <memory>
#include <string>

namespace ResearchImages {
using Bitmap=std::unique_ptr<BSurface,GameDeleter>;
void drawBadge(Surface* surface,int project,RectangleStruct rect);
Bitmap tooltip(int project,int house,const std::wstring& text,int width,int maxHeight,unsigned turn);
}
