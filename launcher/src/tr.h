// The launcher's two languages: the English text is the key, German comes
// from the table in tr.cpp.
#pragma once

#include <QString>

void setUiGerman(bool german);
bool uiGerman();
QString T(const char *english);
