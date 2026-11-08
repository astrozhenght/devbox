#ifndef CALCULATOR_SERVICE_H
#define CALCULATOR_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#define CALCULATOR_INPUT_CAPACITY       24U
#define CALCULATOR_DISPLAY_CAPACITY     32U
#define CALCULATOR_EXPRESSION_CAPACITY  48U

typedef enum
{
    CALCULATOR_KEY_0 = 0,
    CALCULATOR_KEY_1,
    CALCULATOR_KEY_2,
    CALCULATOR_KEY_3,
    CALCULATOR_KEY_4,
    CALCULATOR_KEY_5,
    CALCULATOR_KEY_6,
    CALCULATOR_KEY_7,
    CALCULATOR_KEY_8,
    CALCULATOR_KEY_9,
    CALCULATOR_KEY_DECIMAL,
    CALCULATOR_KEY_SIGN,
    CALCULATOR_KEY_PERCENT,
    CALCULATOR_KEY_BACKSPACE,
    CALCULATOR_KEY_CLEAR,
    CALCULATOR_KEY_ADD,
    CALCULATOR_KEY_SUBTRACT,
    CALCULATOR_KEY_MULTIPLY,
    CALCULATOR_KEY_DIVIDE,
    CALCULATOR_KEY_EQUALS
} calculator_key_t;

typedef enum
{
    CALCULATOR_OPERATOR_NONE = 0,
    CALCULATOR_OPERATOR_ADD,
    CALCULATOR_OPERATOR_SUBTRACT,
    CALCULATOR_OPERATOR_MULTIPLY,
    CALCULATOR_OPERATOR_DIVIDE
} calculator_operator_t;

typedef enum
{
    CALCULATOR_STATUS_OK = 0,
    CALCULATOR_STATUS_DIVIDE_BY_ZERO,
    CALCULATOR_STATUS_OVERFLOW,
    CALCULATOR_STATUS_INVALID_INPUT
} calculator_status_t;

typedef struct
{
    char input[CALCULATOR_INPUT_CAPACITY];
    char display[CALCULATOR_DISPLAY_CAPACITY];
    char expression[CALCULATOR_EXPRESSION_CAPACITY];
    double accumulator;
    double last_operand;
    calculator_operator_t pending_operator;
    calculator_operator_t last_operator;
    calculator_status_t status;
    bool replace_input;
    bool result_shown;
} calculator_service_t;

void calculator_service_init(calculator_service_t *calculator);
calculator_status_t calculator_service_press(calculator_service_t *calculator,
                                             calculator_key_t key);
const char *calculator_service_get_display(const calculator_service_t *calculator);
const char *calculator_service_get_expression(const calculator_service_t *calculator);
calculator_operator_t calculator_service_get_pending_operator(
    const calculator_service_t *calculator);

#endif
