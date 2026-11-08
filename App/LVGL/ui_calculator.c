#include "ui_calculator.h"

#include <stdint.h>

#include "calculator_service.h"
#include "ui_fonts.h"

#define CALCULATOR_UI_BG_COLOR           0xF5F7FA
#define CALCULATOR_UI_CARD_COLOR         0xFFFFFF
#define CALCULATOR_UI_TEXT_COLOR         0x243247
#define CALCULATOR_UI_MUTED_COLOR        0x69788B
#define CALCULATOR_UI_BORDER_COLOR       0xE4EAF1
#define CALCULATOR_UI_PRIMARY_COLOR      0x2196F3
#define CALCULATOR_UI_OPERATOR_BG_COLOR  0xE8F3FE
#define CALCULATOR_UI_FUNCTION_BG_COLOR  0xEEF1F5
#define CALCULATOR_UI_DANGER_COLOR       0xE53935
#define CALCULATOR_UI_TITLE_COLOR        0xE91E63

typedef enum
{
    CALCULATOR_BUTTON_NUMBER = 0,
    CALCULATOR_BUTTON_FUNCTION,
    CALCULATOR_BUTTON_OPERATOR,
    CALCULATOR_BUTTON_EQUALS
} ui_calculator_button_role_t;

typedef struct
{
    const char *text;
    calculator_key_t key;
    ui_calculator_button_role_t role;
} ui_calculator_button_descriptor_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *header;
    lv_obj_t *display_card;
    lv_obj_t *expression_label;
    lv_obj_t *result_label;
    lv_obj_t *keypad;
    lv_obj_t *home_button;
    lv_obj_t *buttons[20];
    calculator_service_t calculator;
} ui_calculator_context_t;

static const ui_calculator_button_descriptor_t calculator_buttons[] =
{
    {"AC", CALCULATOR_KEY_CLEAR,      CALCULATOR_BUTTON_FUNCTION},
    {LV_SYMBOL_BACKSPACE, CALCULATOR_KEY_BACKSPACE, CALCULATOR_BUTTON_FUNCTION},
    {"%",  CALCULATOR_KEY_PERCENT,    CALCULATOR_BUTTON_FUNCTION},
    {"÷",  CALCULATOR_KEY_DIVIDE,     CALCULATOR_BUTTON_OPERATOR},
    {"7",  CALCULATOR_KEY_7,          CALCULATOR_BUTTON_NUMBER},
    {"8",  CALCULATOR_KEY_8,          CALCULATOR_BUTTON_NUMBER},
    {"9",  CALCULATOR_KEY_9,          CALCULATOR_BUTTON_NUMBER},
    {"×",  CALCULATOR_KEY_MULTIPLY,   CALCULATOR_BUTTON_OPERATOR},
    {"4",  CALCULATOR_KEY_4,          CALCULATOR_BUTTON_NUMBER},
    {"5",  CALCULATOR_KEY_5,          CALCULATOR_BUTTON_NUMBER},
    {"6",  CALCULATOR_KEY_6,          CALCULATOR_BUTTON_NUMBER},
    {"-",  CALCULATOR_KEY_SUBTRACT,   CALCULATOR_BUTTON_OPERATOR},
    {"1",  CALCULATOR_KEY_1,          CALCULATOR_BUTTON_NUMBER},
    {"2",  CALCULATOR_KEY_2,          CALCULATOR_BUTTON_NUMBER},
    {"3",  CALCULATOR_KEY_3,          CALCULATOR_BUTTON_NUMBER},
    {"+",  CALCULATOR_KEY_ADD,        CALCULATOR_BUTTON_OPERATOR},
    {"±",  CALCULATOR_KEY_SIGN,       CALCULATOR_BUTTON_FUNCTION},
    {"0",  CALCULATOR_KEY_0,          CALCULATOR_BUTTON_NUMBER},
    {".",  CALCULATOR_KEY_DECIMAL,    CALCULATOR_BUTTON_NUMBER},
    {"=",  CALCULATOR_KEY_EQUALS,     CALCULATOR_BUTTON_EQUALS}
};

static ui_calculator_context_t calculator_context = {0};

static void ui_calculator_update_display(void);

static calculator_operator_t ui_calculator_key_operator(calculator_key_t key)
{
    switch (key)
    {
        case CALCULATOR_KEY_ADD:      return CALCULATOR_OPERATOR_ADD;
        case CALCULATOR_KEY_SUBTRACT: return CALCULATOR_OPERATOR_SUBTRACT;
        case CALCULATOR_KEY_MULTIPLY: return CALCULATOR_OPERATOR_MULTIPLY;
        case CALCULATOR_KEY_DIVIDE:   return CALCULATOR_OPERATOR_DIVIDE;
        default:                      return CALCULATOR_OPERATOR_NONE;
    }
}

static void ui_calculator_root_deleted_cb(lv_event_t *event)
{
    (void)event;
    calculator_context.root = NULL;
}

static void ui_calculator_home_button_cb(lv_event_t *event)
{
    (void)event;
    if (calculator_context.root != NULL)
    {
        lv_obj_del(calculator_context.root);
    }
}

static void ui_calculator_key_event_cb(lv_event_t *event)
{
    calculator_key_t key = (calculator_key_t)(uintptr_t)lv_event_get_user_data(event);

    calculator_service_press(&calculator_context.calculator, key);
    ui_calculator_update_display();
}

static void ui_calculator_style_button(lv_obj_t *button,
                                       ui_calculator_button_role_t role)
{
    lv_color_t background = lv_color_hex(CALCULATOR_UI_CARD_COLOR);
    lv_color_t text = lv_color_hex(CALCULATOR_UI_TEXT_COLOR);

    if (role == CALCULATOR_BUTTON_FUNCTION)
    {
        background = lv_color_hex(CALCULATOR_UI_FUNCTION_BG_COLOR);
    }
    else if (role == CALCULATOR_BUTTON_OPERATOR)
    {
        background = lv_color_hex(CALCULATOR_UI_OPERATOR_BG_COLOR);
        text = lv_color_hex(CALCULATOR_UI_PRIMARY_COLOR);
    }
    else if (role == CALCULATOR_BUTTON_EQUALS)
    {
        background = lv_color_hex(CALCULATOR_UI_PRIMARY_COLOR);
        text = lv_color_white();
    }

    lv_obj_set_style_bg_color(button, background, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(CALCULATOR_UI_BORDER_COLOR), 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_text_color(button, text, 0);
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(CALCULATOR_UI_PRIMARY_COLOR),
                              LV_STATE_CHECKED);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button,
                              lv_color_mix(background,
                                           lv_color_hex(CALCULATOR_UI_PRIMARY_COLOR),
                                           LV_OPA_20),
                              LV_STATE_PRESSED);
}

static void ui_calculator_update_operator_state(void)
{
    calculator_operator_t pending = calculator_service_get_pending_operator(
        &calculator_context.calculator);

    for (uint32_t index = 0U;
         index < (sizeof(calculator_buttons) / sizeof(calculator_buttons[0]));
         index++)
    {
        if (calculator_buttons[index].role != CALCULATOR_BUTTON_OPERATOR)
        {
            continue;
        }

        if (ui_calculator_key_operator(calculator_buttons[index].key) == pending)
        {
            lv_obj_add_state(calculator_context.buttons[index], LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(calculator_context.buttons[index], LV_STATE_CHECKED);
        }
    }
}

static void ui_calculator_update_display(void)
{
    lv_label_set_text(calculator_context.expression_label,
                      calculator_service_get_expression(
                          &calculator_context.calculator));
    lv_label_set_text(calculator_context.result_label,
                      calculator_service_get_display(
                          &calculator_context.calculator));

    /* “错误”使用已补齐中文字形的 24 px 字体，数字结果使用 32 px 字体。 */
    lv_obj_set_style_text_font(calculator_context.result_label,
        calculator_context.calculator.status == CALCULATOR_STATUS_OK
            ? &ui_font_chinese_32_4
            : &ui_font_chinese_24_4,
        0);
    lv_obj_set_style_text_color(calculator_context.result_label,
        calculator_context.calculator.status == CALCULATOR_STATUS_OK
            ? lv_color_hex(CALCULATOR_UI_TEXT_COLOR)
            : lv_color_hex(CALCULATOR_UI_DANGER_COLOR),
        0);
    ui_calculator_update_operator_state();
}

static void ui_calculator_create_header(void)
{
    lv_obj_t *title;
    lv_obj_t *home_label;
    static const lv_coord_t columns[] = {
        LV_GRID_CONTENT, LV_GRID_FR(1), 112, LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t rows[] = {
        LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };

    calculator_context.header = lv_obj_create(calculator_context.root);
    lv_obj_remove_style_all(calculator_context.header);
    lv_obj_clear_flag(calculator_context.header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(calculator_context.header, 0, 0);
    lv_obj_set_style_pad_all(calculator_context.header, 0, 0);
    lv_obj_set_style_pad_gap(calculator_context.header, 8, 0);
    lv_obj_set_style_pad_left(calculator_context.header, 10, 0);
    lv_obj_set_style_radius(calculator_context.header, 0, 0);
    lv_obj_set_grid_dsc_array(calculator_context.header, columns, rows);
    lv_obj_set_style_grid_row_align(calculator_context.header,
                                    LV_GRID_ALIGN_SPACE_BETWEEN, 0);

    title = lv_label_create(calculator_context.header);
    lv_obj_set_style_text_font(title, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CALCULATOR_UI_TITLE_COLOR), 0);
    lv_label_set_text(title, "计算器");
    lv_obj_set_grid_cell(title,
                         LV_GRID_ALIGN_START, 0, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);

    calculator_context.home_button = lv_btn_create(calculator_context.header);
    lv_obj_remove_style_all(calculator_context.home_button);
    lv_obj_set_size(calculator_context.home_button, 112, 38);
    lv_obj_set_style_pad_all(calculator_context.home_button, 3, 0);
    lv_obj_set_style_radius(calculator_context.home_button, 6, 0);
    lv_obj_set_style_bg_opa(calculator_context.home_button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(calculator_context.home_button,
                              lv_color_white(), 0);
    lv_obj_set_style_bg_color(calculator_context.home_button,
                              lv_palette_main(LV_PALETTE_RED),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(calculator_context.home_button, 1, 0);
    lv_obj_set_style_border_color(calculator_context.home_button,
                                  lv_color_hex(CALCULATOR_UI_BORDER_COLOR), 0);
    lv_obj_set_style_text_font(calculator_context.home_button,
                               &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(calculator_context.home_button,
                                lv_color_hex(CALCULATOR_UI_TEXT_COLOR), 0);
    lv_obj_set_style_text_color(calculator_context.home_button,
                                lv_color_white(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(calculator_context.home_button,
                        ui_calculator_home_button_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_set_grid_cell(calculator_context.home_button,
                         LV_GRID_ALIGN_END, 2, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);

    home_label = lv_label_create(calculator_context.home_button);
    lv_label_set_text(home_label, "返回主页");
    lv_obj_center(home_label);
}

static void ui_calculator_create_display(void)
{
    calculator_context.display_card = lv_obj_create(calculator_context.root);
    lv_obj_clear_flag(calculator_context.display_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(calculator_context.display_card,
                              lv_color_hex(CALCULATOR_UI_CARD_COLOR), 0);
    lv_obj_set_style_bg_opa(calculator_context.display_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(calculator_context.display_card, 1, 0);
    lv_obj_set_style_border_color(calculator_context.display_card,
                                  lv_color_hex(CALCULATOR_UI_BORDER_COLOR), 0);
    lv_obj_set_style_radius(calculator_context.display_card, 8, 0);
    lv_obj_set_style_pad_hor(calculator_context.display_card, 16, 0);
    lv_obj_set_style_pad_ver(calculator_context.display_card, 6, 0);

    calculator_context.expression_label = lv_label_create(
        calculator_context.display_card);
    lv_obj_set_width(calculator_context.expression_label, lv_pct(100));
    lv_obj_set_style_text_font(calculator_context.expression_label,
                               &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(calculator_context.expression_label,
                                lv_color_hex(CALCULATOR_UI_MUTED_COLOR), 0);
    lv_obj_set_style_text_align(calculator_context.expression_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(calculator_context.expression_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    calculator_context.result_label = lv_label_create(calculator_context.display_card);
    lv_obj_set_width(calculator_context.result_label, lv_pct(100));
    lv_obj_set_style_text_align(calculator_context.result_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(calculator_context.result_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void ui_calculator_create_keypad(void)
{
    static const lv_coord_t columns[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t rows[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };

    calculator_context.keypad = lv_obj_create(calculator_context.root);
    lv_obj_clear_flag(calculator_context.keypad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(calculator_context.keypad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calculator_context.keypad, 0, 0);
    lv_obj_set_style_pad_all(calculator_context.keypad, 0, 0);
    lv_obj_set_style_pad_row(calculator_context.keypad, 6, 0);
    lv_obj_set_style_pad_column(calculator_context.keypad, 6, 0);
    lv_obj_set_grid_dsc_array(calculator_context.keypad, columns, rows);

    for (uint32_t index = 0U;
         index < (sizeof(calculator_buttons) / sizeof(calculator_buttons[0]));
         index++)
    {
        lv_obj_t *label;
        lv_obj_t *button = lv_btn_create(calculator_context.keypad);
        calculator_context.buttons[index] = button;
        ui_calculator_style_button(button, calculator_buttons[index].role);
        lv_obj_set_grid_cell(button,
                             LV_GRID_ALIGN_STRETCH, index % 4U, 1,
                             LV_GRID_ALIGN_STRETCH, index / 4U, 1);
        lv_obj_add_event_cb(button,
                            ui_calculator_key_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)calculator_buttons[index].key);

        label = lv_label_create(button);
        lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
        lv_label_set_text(label, calculator_buttons[index].text);
        lv_obj_center(label);
    }
}

void ui_calculator_create(lv_obj_t *parent)
{
    static const lv_coord_t columns[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t rows[] = {42, 82, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    if (calculator_context.root != NULL)
    {
        lv_obj_move_foreground(calculator_context.root);
        return;
    }

    calculator_service_init(&calculator_context.calculator);

    calculator_context.root = lv_obj_create(parent);
    lv_obj_set_size(calculator_context.root, lv_pct(100), lv_pct(92));
    lv_obj_align(calculator_context.root, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(calculator_context.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(calculator_context.root,
                              lv_color_hex(CALCULATOR_UI_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(calculator_context.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(calculator_context.root, 0, 0);
    lv_obj_set_style_radius(calculator_context.root, 0, 0);
    lv_obj_set_style_pad_all(calculator_context.root, 8, 0);
    lv_obj_set_style_pad_row(calculator_context.root, 8, 0);
    lv_obj_set_grid_dsc_array(calculator_context.root, columns, rows);
    lv_obj_add_event_cb(calculator_context.root,
                        ui_calculator_root_deleted_cb,
                        LV_EVENT_DELETE,
                        NULL);

    ui_calculator_create_header();
    ui_calculator_create_display();
    ui_calculator_create_keypad();

    lv_obj_set_grid_cell(calculator_context.header,
                         LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(calculator_context.display_card,
                         LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(calculator_context.keypad,
                         LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 2, 1);

    ui_calculator_update_display();
    lv_obj_move_foreground(calculator_context.root);
}
