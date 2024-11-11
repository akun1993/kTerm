/*
 * Return the width of a string in the font used in GTK controls. Used
 * as a means of picking a sensible size for dialog boxes and pieces
 * of them, in a way that should adapt sensibly to changes in font and
 * resolution.
 */

#include <qtmisc.h>

int string_width(const char *text) {
  int ret;
  get_label_text_dimensions(text, &ret, NULL);
  return ret;
}
