/** @file
  Product-neutral LVGL v9 core controls.
**/
#ifndef LVGL_UI_H_
#define LVGL_UI_H_

#include <Library/LvglLib.h>

typedef struct {
  lv_color_t       Background;
  lv_color_t       Panel;
  lv_color_t       Raised;
  lv_color_t       Line;
  lv_color_t       Text;
  lv_color_t       MutedText;
  lv_color_t       Accent;
  lv_color_t       AccentText;
  lv_color_t       Danger;
  lv_color_t       DangerText;
  const lv_font_t  *Font;
  lv_coord_t       ControlRadius;
  lv_coord_t       PanelRadius;
  lv_coord_t       Padding;
  lv_coord_t       Gap;
  lv_coord_t       OutlineWidth;
  lv_coord_t       OutlinePad;
} LVGL_UI_THEME;

typedef enum {
  LVGL_UI_BUTTON_GHOST,
  LVGL_UI_BUTTON_PRIMARY,
  LVGL_UI_BUTTON_DANGER
} LVGL_UI_BUTTON_KIND;

void
LvglUiThemeInitNeutral (
  LVGL_UI_THEME  *Theme
  );

lv_obj_t *
LvglUiCreateButton (
  lv_obj_t                 *Parent,
  const char               *Text,
  LVGL_UI_BUTTON_KIND      Kind,
  const LVGL_UI_THEME      *Theme
  );

lv_obj_t *
LvglUiCreateChoiceCard (
  lv_obj_t                 *Parent,
  const char               *Title,
  const char               *Description,
  lv_color_t               Accent,
  const LVGL_UI_THEME      *Theme
  );

lv_obj_t *
LvglUiCreateSelectableRow (
  lv_obj_t                 *Parent,
  const char               *Title,
  const char               *Detail,
  lv_color_t               Accent,
  const LVGL_UI_THEME      *Theme
  );

#endif
