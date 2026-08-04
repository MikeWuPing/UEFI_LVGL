/** @file
  Product-neutral LVGL v9 core controls.
**/

#include <Library/LvglUi.h>

static LVGL_UI_THEME  mNeutralTheme;
static bool           mNeutralThemeReady;

static
const LVGL_UI_THEME *
ResolveTheme (
  const LVGL_UI_THEME  *Theme
  )
{
  if (Theme != NULL) {
    return Theme;
  }

  if (!mNeutralThemeReady) {
    LvglUiThemeInitNeutral (&mNeutralTheme);
    mNeutralThemeReady = true;
  }

  return &mNeutralTheme;
}

static
void
StyleLabel (
  lv_obj_t             *Label,
  const LVGL_UI_THEME  *Theme,
  lv_color_t           Color
  )
{
  lv_obj_remove_style_all (Label);
  lv_obj_set_width (Label, lv_pct (100));
  lv_obj_set_height (Label, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa (Label, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color (Label, Color, 0);
  lv_obj_set_style_text_font (
    Label,
    (Theme->Font != NULL) ? Theme->Font : &lv_font_montserrat_14,
    0
    );
}

static
lv_obj_t *
CreateText (
  lv_obj_t             *Parent,
  const char           *Text,
  const LVGL_UI_THEME  *Theme,
  lv_color_t           Color
  )
{
  lv_obj_t  *Label;

  Label = lv_label_create (Parent);
  if (Label == NULL) {
    return NULL;
  }

  lv_label_set_text (Label, (Text != NULL) ? Text : "");
  lv_label_set_long_mode (Label, LV_LABEL_LONG_WRAP);
  StyleLabel (Label, Theme, Color);
  return Label;
}

static
void
StyleControlBase (
  lv_obj_t             *Object,
  const LVGL_UI_THEME  *Theme,
  lv_color_t           Background,
  lv_opa_t             BackgroundOpacity,
  lv_color_t           Border,
  lv_coord_t           Radius,
  lv_coord_t           MinimumHeight
  )
{
  lv_obj_remove_style_all (Object);
  lv_obj_set_width (Object, lv_pct (100));
  lv_obj_set_height (Object, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height (Object, MinimumHeight, 0);
  lv_obj_set_style_bg_color (Object, Background, 0);
  lv_obj_set_style_bg_opa (Object, BackgroundOpacity, 0);
  lv_obj_set_style_border_color (Object, Border, 0);
  lv_obj_set_style_border_opa (Object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width (Object, 1, 0);
  lv_obj_set_style_radius (Object, Radius, 0);
  lv_obj_set_style_pad_all (Object, Theme->Padding, 0);
  lv_obj_set_style_pad_gap (Object, Theme->Gap, 0);
  lv_obj_remove_flag (Object, LV_OBJ_FLAG_SCROLLABLE);
}

static
void
StyleInteractiveStates (
  lv_obj_t             *Object,
  const LVGL_UI_THEME  *Theme,
  lv_color_t           Accent,
  lv_color_t           PressedBackground
  )
{
  lv_style_selector_t  Selector;

  Selector = LV_PART_MAIN | LV_STATE_FOCUSED;
  lv_obj_set_style_outline_color (Object, Accent, Selector);
  lv_obj_set_style_outline_opa (Object, LV_OPA_COVER, Selector);
  lv_obj_set_style_outline_width (Object, Theme->OutlineWidth, Selector);
  lv_obj_set_style_outline_pad (Object, Theme->OutlinePad, Selector);
  lv_obj_set_style_border_color (Object, Accent, Selector);

  Selector = LV_PART_MAIN | LV_STATE_PRESSED;
  lv_obj_set_style_bg_color (Object, PressedBackground, Selector);
  lv_obj_set_style_bg_opa (Object, LV_OPA_COVER, Selector);
  lv_obj_set_style_border_color (Object, Accent, Selector);

  Selector = LV_PART_MAIN | LV_STATE_DISABLED;
  lv_obj_set_style_bg_color (Object, Theme->Panel, Selector);
  lv_obj_set_style_bg_opa (Object, LV_OPA_COVER, Selector);
  lv_obj_set_style_border_color (Object, Theme->Line, Selector);
  lv_obj_set_style_opa (Object, LV_OPA_50, Selector);
}

static
void
StyleCheckedState (
  lv_obj_t             *Object,
  const LVGL_UI_THEME  *Theme,
  lv_color_t           Accent
  )
{
  lv_style_selector_t  Selector;

  Selector = LV_PART_MAIN | LV_STATE_CHECKED;
  lv_obj_set_style_bg_color (Object, Theme->Raised, Selector);
  lv_obj_set_style_bg_opa (Object, LV_OPA_COVER, Selector);
  lv_obj_set_style_border_color (Object, Accent, Selector);
  lv_obj_set_style_border_width (Object, 2, Selector);
}

void
LvglUiThemeInitNeutral (
  LVGL_UI_THEME  *Theme
  )
{
  if (Theme == NULL) {
    return;
  }

  Theme->Background    = lv_color_hex (0x202124);
  Theme->Panel         = lv_color_hex (0x303134);
  Theme->Raised        = lv_color_hex (0x3C4043);
  Theme->Line          = lv_color_hex (0x5F6368);
  Theme->Text          = lv_color_hex (0xF1F3F4);
  Theme->MutedText     = lv_color_hex (0xBDC1C6);
  Theme->Accent        = lv_color_hex (0x8AB4F8);
  Theme->AccentText    = lv_color_hex (0x202124);
  Theme->Danger        = lv_color_hex (0xF28B82);
  Theme->DangerText    = lv_color_hex (0x202124);
  Theme->Font          = &lv_font_montserrat_14;
  Theme->ControlRadius = 6;
  Theme->PanelRadius   = 8;
  Theme->Padding       = 12;
  Theme->Gap           = 8;
  Theme->OutlineWidth  = 2;
  Theme->OutlinePad    = 2;
}

lv_obj_t *
LvglUiCreateButton (
  lv_obj_t                 *Parent,
  const char               *Text,
  LVGL_UI_BUTTON_KIND      Kind,
  const LVGL_UI_THEME      *Theme
  )
{
  const LVGL_UI_THEME  *Resolved;
  lv_obj_t             *Button;
  lv_obj_t             *Label;
  lv_color_t           Background;
  lv_color_t           Border;
  lv_color_t           TextColor;
  lv_color_t           StateAccent;
  lv_opa_t             BackgroundOpacity;

  if ((Parent == NULL) ||
      (Kind < LVGL_UI_BUTTON_GHOST) ||
      (Kind > LVGL_UI_BUTTON_DANGER)) {
    return NULL;
  }

  Resolved = ResolveTheme (Theme);
  Background = Resolved->Panel;
  BackgroundOpacity = LV_OPA_TRANSP;
  Border = Resolved->Line;
  TextColor = Resolved->Text;
  StateAccent = Resolved->Accent;

  if (Kind == LVGL_UI_BUTTON_PRIMARY) {
    Background = Resolved->Accent;
    BackgroundOpacity = LV_OPA_COVER;
    Border = Resolved->Accent;
    TextColor = Resolved->AccentText;
  } else if (Kind == LVGL_UI_BUTTON_DANGER) {
    Background = Resolved->Danger;
    BackgroundOpacity = LV_OPA_COVER;
    Border = Resolved->Danger;
    TextColor = Resolved->DangerText;
    StateAccent = Resolved->Danger;
  }

  Button = lv_button_create (Parent);
  if (Button == NULL) {
    return NULL;
  }

  StyleControlBase (
    Button,
    Resolved,
    Background,
    BackgroundOpacity,
    Border,
    Resolved->ControlRadius,
    40
    );
  StyleInteractiveStates (
    Button,
    Resolved,
    StateAccent,
    lv_color_darken (Background, LV_OPA_20)
    );
  lv_obj_set_flex_flow (Button, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align (
    Button,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER
    );
  lv_obj_add_flag (Button, LV_OBJ_FLAG_CLICK_FOCUSABLE);

  Label = CreateText (Button, Text, Resolved, TextColor);
  if (Label == NULL) {
    lv_obj_delete (Button);
    return NULL;
  }

  lv_obj_set_style_text_align (Label, LV_TEXT_ALIGN_CENTER, 0);
  return Button;
}

lv_obj_t *
LvglUiCreateChoiceCard (
  lv_obj_t                 *Parent,
  const char               *Title,
  const char               *Description,
  lv_color_t               Accent,
  const LVGL_UI_THEME      *Theme
  )
{
  const LVGL_UI_THEME  *Resolved;
  lv_obj_t             *Card;
  lv_obj_t             *TitleLabel;
  lv_obj_t             *DescriptionLabel;

  if (Parent == NULL) {
    return NULL;
  }

  Resolved = ResolveTheme (Theme);
  Card = lv_button_create (Parent);
  if (Card == NULL) {
    return NULL;
  }

  StyleControlBase (
    Card,
    Resolved,
    Resolved->Panel,
    LV_OPA_COVER,
    Resolved->Line,
    Resolved->PanelRadius,
    76
    );
  StyleCheckedState (Card, Resolved, Accent);
  StyleInteractiveStates (
    Card,
    Resolved,
    Accent,
    lv_color_darken (Resolved->Raised, LV_OPA_20)
    );
  lv_obj_set_flex_flow (Card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align (
    Card,
    LV_FLEX_ALIGN_START,
    LV_FLEX_ALIGN_START,
    LV_FLEX_ALIGN_START
    );
  lv_obj_add_flag (Card, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_flag (Card, LV_OBJ_FLAG_CLICK_FOCUSABLE);

  TitleLabel = CreateText (Card, Title, Resolved, Resolved->Text);
  if (TitleLabel == NULL) {
    lv_obj_delete (Card);
    return NULL;
  }

  DescriptionLabel = CreateText (
                       Card,
                       Description,
                       Resolved,
                       Resolved->MutedText
                       );
  if (DescriptionLabel == NULL) {
    lv_obj_delete (Card);
    return NULL;
  }

  return Card;
}

lv_obj_t *
LvglUiCreateSelectableRow (
  lv_obj_t                 *Parent,
  const char               *Title,
  const char               *Detail,
  lv_color_t               Accent,
  const LVGL_UI_THEME      *Theme
  )
{
  const LVGL_UI_THEME  *Resolved;
  lv_obj_t             *Row;
  lv_obj_t             *TitleLabel;
  lv_obj_t             *DetailLabel;

  if (Parent == NULL) {
    return NULL;
  }

  Resolved = ResolveTheme (Theme);
  Row = lv_button_create (Parent);
  if (Row == NULL) {
    return NULL;
  }

  StyleControlBase (
    Row,
    Resolved,
    Resolved->Panel,
    LV_OPA_COVER,
    Resolved->Line,
    Resolved->ControlRadius,
    60
    );
  StyleCheckedState (Row, Resolved, Accent);
  StyleInteractiveStates (
    Row,
    Resolved,
    Accent,
    lv_color_darken (Resolved->Raised, LV_OPA_20)
    );
  lv_obj_set_flex_flow (Row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align (
    Row,
    LV_FLEX_ALIGN_START,
    LV_FLEX_ALIGN_START,
    LV_FLEX_ALIGN_START
    );
  lv_obj_add_flag (Row, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_flag (Row, LV_OBJ_FLAG_CLICK_FOCUSABLE);

  TitleLabel = CreateText (Row, Title, Resolved, Resolved->Text);
  if (TitleLabel == NULL) {
    lv_obj_delete (Row);
    return NULL;
  }

  DetailLabel = CreateText (Row, Detail, Resolved, Resolved->MutedText);
  if (DetailLabel == NULL) {
    lv_obj_delete (Row);
    return NULL;
  }

  return Row;
}
