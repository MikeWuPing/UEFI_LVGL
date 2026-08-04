/** @file
  Host tests for the product-neutral LVGL core controls.
**/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <Library/LvglUi.h>

static void
FlushReady (
  lv_display_t    *Display,
  const lv_area_t *Area,
  uint8_t         *Pixels
  )
{
  LV_UNUSED (Area);
  LV_UNUSED (Pixels);
  lv_display_flush_ready (Display);
}

static void
AssertContained (
  const lv_obj_t  *Container,
  const lv_obj_t  *Child
  )
{
  lv_area_t  ContainerArea;
  lv_area_t  ChildArea;

  lv_obj_get_coords (Container, &ContainerArea);
  lv_obj_get_coords (Child, &ChildArea);
  assert (ChildArea.x1 >= ContainerArea.x1);
  assert (ChildArea.y1 >= ContainerArea.y1);
  assert (ChildArea.x2 <= ContainerArea.x2);
  assert (ChildArea.y2 <= ContainerArea.y2);
}

static void
AssertTextLayout (
  lv_obj_t  *Control,
  uint32_t  ChildCount
  )
{
  lv_obj_t  *Previous;
  lv_obj_t  *Child;
  lv_area_t PreviousArea;
  lv_area_t ChildArea;
  uint32_t  Index;

  lv_obj_update_layout (Control);
  assert (lv_obj_get_child_count (Control) == ChildCount);
  Previous = NULL;
  for (Index = 0; Index < ChildCount; Index++) {
    Child = lv_obj_get_child (Control, (int32_t)Index);
    assert (Child != NULL);
    AssertContained (Control, Child);
    if (Previous != NULL) {
      lv_obj_get_coords (Previous, &PreviousArea);
      lv_obj_get_coords (Child, &ChildArea);
      assert (PreviousArea.y2 < ChildArea.y1);
    }

    Previous = Child;
  }
}

static void
TestResponsiveLayout (
  lv_obj_t              *Screen,
  const LVGL_UI_THEME   *Theme,
  int32_t               Width
  )
{
  static const char  LongTitle[] =
    "A product-neutral title that wraps cleanly on narrow firmware screens";
  static const char  LongDetail[] =
    "A longer supporting description must remain inside the control and never overlap adjacent text.";
  lv_obj_t           *Host;
  lv_obj_t           *Button;
  lv_obj_t           *Choice;
  lv_obj_t           *Row;

  Host = lv_obj_create (Screen);
  assert (Host != NULL);
  lv_obj_remove_style_all (Host);
  lv_obj_set_size (Host, Width, 460);

  Button = LvglUiCreateButton (
             Host,
             LongTitle,
             LVGL_UI_BUTTON_PRIMARY,
             Theme
             );
  assert (Button != NULL);
  lv_obj_set_width (Button, lv_pct (100));
  lv_obj_update_layout (Button);
  assert (lv_obj_get_width (Button) == Width);
  AssertContained (Button, lv_obj_get_child (Button, 0));

  lv_obj_delete (Button);
  Choice = LvglUiCreateChoiceCard (
             Host,
             LongTitle,
             LongDetail,
             Theme->Accent,
             Theme
             );
  assert (Choice != NULL);
  lv_obj_update_layout (Choice);
  assert (lv_obj_get_width (Choice) == Width);
  AssertTextLayout (Choice, 2);

  lv_obj_delete (Choice);
  Row = LvglUiCreateSelectableRow (
          Host,
          LongTitle,
          LongDetail,
          Theme->Accent,
          Theme
          );
  assert (Row != NULL);
  lv_obj_update_layout (Row);
  assert (lv_obj_get_width (Row) == Width);
  AssertTextLayout (Row, 2);

  lv_obj_delete (Host);
}

static void
TestStates (
  lv_obj_t              *Parent,
  const LVGL_UI_THEME   *Theme
  )
{
  lv_obj_t    *Button;
  lv_obj_t    *Choice;
  lv_color_t  DefaultColor;
  lv_color_t  StateColor;
  int32_t     DefaultWidth;
  lv_opa_t    DefaultOpacity;

  Button = LvglUiCreateButton (
             Parent,
             "Apply",
             LVGL_UI_BUTTON_PRIMARY,
             Theme
             );
  assert (Button != NULL);
  DefaultColor = lv_obj_get_style_bg_color (Button, LV_PART_MAIN);
  lv_obj_add_state (Button, LV_STATE_PRESSED);
  StateColor = lv_obj_get_style_bg_color (Button, LV_PART_MAIN);
  assert (!lv_color_eq (DefaultColor, StateColor));
  lv_obj_remove_state (Button, LV_STATE_PRESSED);

  Choice = LvglUiCreateChoiceCard (
             Parent,
             "Balanced",
             "Use neutral defaults",
             Theme->Accent,
             Theme
             );
  assert (Choice != NULL);
  DefaultColor = lv_obj_get_style_bg_color (Choice, LV_PART_MAIN);
  DefaultWidth = lv_obj_get_style_border_width (Choice, LV_PART_MAIN);
  lv_obj_add_state (Choice, LV_STATE_CHECKED);
  StateColor = lv_obj_get_style_bg_color (Choice, LV_PART_MAIN);
  assert (!lv_color_eq (DefaultColor, StateColor));
  assert (lv_obj_get_style_border_width (Choice, LV_PART_MAIN) != DefaultWidth);
  lv_obj_remove_state (Choice, LV_STATE_CHECKED);

  DefaultWidth = lv_obj_get_style_outline_width (Choice, LV_PART_MAIN);
  lv_obj_add_state (Choice, LV_STATE_FOCUSED);
  assert (lv_obj_get_style_outline_width (Choice, LV_PART_MAIN) != DefaultWidth);
  lv_obj_remove_state (Choice, LV_STATE_FOCUSED);

  DefaultColor = lv_obj_get_style_bg_color (Choice, LV_PART_MAIN);
  lv_obj_add_state (Choice, LV_STATE_PRESSED);
  StateColor = lv_obj_get_style_bg_color (Choice, LV_PART_MAIN);
  assert (!lv_color_eq (DefaultColor, StateColor));
  lv_obj_remove_state (Choice, LV_STATE_PRESSED);

  DefaultOpacity = lv_obj_get_style_opa (Choice, LV_PART_MAIN);
  lv_obj_add_state (Choice, LV_STATE_DISABLED);
  assert (lv_obj_get_style_opa (Choice, LV_PART_MAIN) != DefaultOpacity);

  lv_obj_delete (Button);
  lv_obj_delete (Choice);
}

int
main (
  void
  )
{
  static uint32_t  DrawBuffer[640 * 60];
  LVGL_UI_THEME    Theme;
  lv_display_t     *Display;
  lv_obj_t         *Screen;
  lv_obj_t         *Root;
  lv_obj_t         *Button;
  lv_obj_t         *Choice;
  lv_obj_t         *Row;
  uint32_t         ChildCount;

  lv_init ();
  Display = lv_display_create (640, 480);
  assert (Display != NULL);
  lv_display_set_flush_cb (Display, FlushReady);
  lv_display_set_buffers (
    Display,
    DrawBuffer,
    NULL,
    sizeof (DrawBuffer),
    LV_DISPLAY_RENDER_MODE_PARTIAL
    );

  Screen = lv_screen_active ();
  assert (Screen != NULL);
  Root = lv_obj_create (Screen);
  assert (Root != NULL);
  lv_obj_remove_style_all (Root);
  lv_obj_set_size (Root, lv_pct (100), lv_pct (100));

  LvglUiThemeInitNeutral (NULL);
  LvglUiThemeInitNeutral (&Theme);
  assert (Theme.Font == &lv_font_montserrat_14);
  assert (Theme.ControlRadius <= 8);
  assert (Theme.PanelRadius <= 8);

  ChildCount = lv_obj_get_child_count (Root);
  assert (LvglUiCreateButton (
            NULL,
            "Invalid",
            LVGL_UI_BUTTON_PRIMARY,
            &Theme
            ) == NULL);
  assert (LvglUiCreateButton (
            Root,
            "Invalid",
            (LVGL_UI_BUTTON_KIND)-1,
            &Theme
            ) == NULL);
  assert (LvglUiCreateButton (
            Root,
            "Invalid",
            (LVGL_UI_BUTTON_KIND)3,
            &Theme
            ) == NULL);
  assert (LvglUiCreateChoiceCard (
            NULL,
            "Invalid",
            "Invalid",
            Theme.Accent,
            &Theme
            ) == NULL);
  assert (LvglUiCreateSelectableRow (
            NULL,
            "Invalid",
            "Invalid",
            Theme.Accent,
            &Theme
            ) == NULL);
  assert (lv_obj_get_child_count (Root) == ChildCount);

  Button = LvglUiCreateButton (
             Root,
             NULL,
             LVGL_UI_BUTTON_GHOST,
             NULL
             );
  assert (Button != NULL);
  assert (lv_obj_get_child_count (Button) == 1);
  assert (strcmp (lv_label_get_text (lv_obj_get_child (Button, 0)), "") == 0);
  assert (lv_obj_get_style_text_font (
            lv_obj_get_child (Button, 0),
            LV_PART_MAIN
            ) == &lv_font_montserrat_14);

  Choice = LvglUiCreateChoiceCard (
             Root,
             NULL,
             NULL,
             Theme.Accent,
             &Theme
             );
  assert (Choice != NULL);
  assert (lv_obj_has_flag (Choice, LV_OBJ_FLAG_CHECKABLE));
  assert (lv_obj_get_child_count (Choice) == 2);
  assert (strcmp (lv_label_get_text (lv_obj_get_child (Choice, 0)), "") == 0);
  assert (strcmp (lv_label_get_text (lv_obj_get_child (Choice, 1)), "") == 0);

  Row = LvglUiCreateSelectableRow (
          Root,
          NULL,
          NULL,
          Theme.Accent,
          &Theme
          );
  assert (Row != NULL);
  assert (lv_obj_has_flag (Row, LV_OBJ_FLAG_CHECKABLE));
  assert (lv_obj_get_child_count (Row) == 2);

  ChildCount = lv_obj_get_child_count (Root);
  lv_obj_delete (Button);
  lv_obj_delete (Choice);
  lv_obj_delete (Row);
  assert (lv_obj_get_child_count (Root) == (ChildCount - 3));

  TestStates (Root, &Theme);
  TestResponsiveLayout (Screen, &Theme, 240);
  TestResponsiveLayout (Screen, &Theme, 320);
  TestResponsiveLayout (Screen, &Theme, 640);

  lv_display_delete (Display);
  lv_deinit ();
  puts ("PASS: LvglUi core controls host tests");
  return 0;
}
