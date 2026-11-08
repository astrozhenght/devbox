#include "calculator_service.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CALCULATOR_ERROR_TEXT  "错误"

static void calculator_reset(calculator_service_t *calculator)
{
    memset(calculator, 0, sizeof(*calculator));
    strcpy(calculator->input, "0");
    strcpy(calculator->display, "0");
    calculator->replace_input = true;
}

static double calculator_current_value(const calculator_service_t *calculator)
{
    char *end = NULL;
    double value = strtod(calculator->input, &end);

    if ((end == calculator->input) || (*end != '\0'))
    {
        return 0.0;
    }

    return value;
}

static bool calculator_format_value(double value, char *output, size_t output_capacity)
{
    int length;

    if (!isfinite(value))
    {
        return false;
    }

    if (fabs(value) < DBL_EPSILON)
    {
        value = 0.0;
    }

    length = snprintf(output, output_capacity, "%.12g", value);
    return (length > 0) && ((size_t)length < output_capacity);
}

static const char *calculator_operator_text(calculator_operator_t operation)
{
    switch (operation)
    {
        case CALCULATOR_OPERATOR_ADD:      return "+";
        case CALCULATOR_OPERATOR_SUBTRACT: return "-";
        case CALCULATOR_OPERATOR_MULTIPLY: return "*";
        case CALCULATOR_OPERATOR_DIVIDE:   return "/";
        default:                           return "";
    }
}

static calculator_operator_t calculator_key_to_operator(calculator_key_t key)
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

static calculator_status_t calculator_set_error(calculator_service_t *calculator,
                                                calculator_status_t status)
{
    calculator->status = status;
    calculator->pending_operator = CALCULATOR_OPERATOR_NONE;
    calculator->replace_input = true;
    calculator->result_shown = false;
    strcpy(calculator->display, CALCULATOR_ERROR_TEXT);
    strcpy(calculator->expression, CALCULATOR_ERROR_TEXT);
    return status;
}

static calculator_status_t calculator_set_value(calculator_service_t *calculator,
                                                double value)
{
    if (!calculator_format_value(value, calculator->input, sizeof(calculator->input)))
    {
        return calculator_set_error(calculator, CALCULATOR_STATUS_OVERFLOW);
    }

    strcpy(calculator->display, calculator->input);
    calculator->status = CALCULATOR_STATUS_OK;
    return CALCULATOR_STATUS_OK;
}

static calculator_status_t calculator_apply(calculator_service_t *calculator,
                                            calculator_operator_t operation,
                                            double left,
                                            double right,
                                            double *result)
{
    switch (operation)
    {
        case CALCULATOR_OPERATOR_ADD:
            *result = left + right;
            break;

        case CALCULATOR_OPERATOR_SUBTRACT:
            *result = left - right;
            break;

        case CALCULATOR_OPERATOR_MULTIPLY:
            *result = left * right;
            break;

        case CALCULATOR_OPERATOR_DIVIDE:
            if (right == 0.0)
            {
                return calculator_set_error(calculator,
                                            CALCULATOR_STATUS_DIVIDE_BY_ZERO);
            }
            *result = left / right;
            break;

        default:
            return calculator_set_error(calculator,
                                        CALCULATOR_STATUS_INVALID_INPUT);
    }

    if (!isfinite(*result))
    {
        return calculator_set_error(calculator, CALCULATOR_STATUS_OVERFLOW);
    }

    return CALCULATOR_STATUS_OK;
}

static void calculator_begin_new_input(calculator_service_t *calculator)
{
    calculator->input[0] = '\0';
    calculator->replace_input = false;
    calculator->result_shown = false;
}

static calculator_status_t calculator_input_digit(calculator_service_t *calculator,
                                                  uint8_t digit)
{
    size_t length;

    if (calculator->status != CALCULATOR_STATUS_OK)
    {
        calculator_reset(calculator);
    }

    if (calculator->result_shown &&
        (calculator->pending_operator == CALCULATOR_OPERATOR_NONE))
    {
        calculator_reset(calculator);
    }

    if (calculator->replace_input)
    {
        calculator_begin_new_input(calculator);
    }

    length = strlen(calculator->input);
    if ((length + 1U) >= sizeof(calculator->input))
    {
        return calculator->status;
    }

    if ((length == 1U) && (calculator->input[0] == '0'))
    {
        calculator->input[0] = (char)('0' + digit);
    }
    else
    {
        calculator->input[length] = (char)('0' + digit);
        calculator->input[length + 1U] = '\0';
    }

    strcpy(calculator->display, calculator->input);
    return calculator->status;
}

static calculator_status_t calculator_input_decimal(calculator_service_t *calculator)
{
    if (calculator->status != CALCULATOR_STATUS_OK)
    {
        calculator_reset(calculator);
    }

    if (calculator->result_shown &&
        (calculator->pending_operator == CALCULATOR_OPERATOR_NONE))
    {
        calculator_reset(calculator);
    }

    if (calculator->replace_input)
    {
        strcpy(calculator->input, "0.");
        calculator->replace_input = false;
        calculator->result_shown = false;
    }
    else if (strchr(calculator->input, '.') == NULL)
    {
        size_t length = strlen(calculator->input);
        if ((length + 1U) < sizeof(calculator->input))
        {
            calculator->input[length] = '.';
            calculator->input[length + 1U] = '\0';
        }
    }

    strcpy(calculator->display, calculator->input);
    return calculator->status;
}

static calculator_status_t calculator_transform_input(calculator_service_t *calculator,
                                                      double scale)
{
    double value;

    if (calculator->status != CALCULATOR_STATUS_OK)
    {
        calculator_reset(calculator);
    }

    if (calculator->result_shown &&
        (calculator->pending_operator == CALCULATOR_OPERATOR_NONE))
    {
        calculator->result_shown = false;
    }

    value = calculator->replace_input &&
            (calculator->pending_operator != CALCULATOR_OPERATOR_NONE)
                ? 0.0
                : calculator_current_value(calculator);
    calculator->replace_input = false;
    return calculator_set_value(calculator, value * scale);
}

static calculator_status_t calculator_select_operator(calculator_service_t *calculator,
                                                      calculator_operator_t operation)
{
    double value = calculator_current_value(calculator);

    if (calculator->status != CALCULATOR_STATUS_OK)
    {
        return calculator->status;
    }

    if ((calculator->pending_operator != CALCULATOR_OPERATOR_NONE) &&
        !calculator->replace_input)
    {
        double result;
        calculator_status_t status = calculator_apply(calculator,
                                                       calculator->pending_operator,
                                                       calculator->accumulator,
                                                       value,
                                                       &result);
        if (status != CALCULATOR_STATUS_OK)
        {
            return status;
        }
        calculator->accumulator = result;
        if (calculator_set_value(calculator, result) != CALCULATOR_STATUS_OK)
        {
            return calculator->status;
        }
    }
    else if (calculator->pending_operator == CALCULATOR_OPERATOR_NONE)
    {
        calculator->accumulator = value;
    }

    calculator->pending_operator = operation;
    calculator->replace_input = true;
    calculator->result_shown = false;
    snprintf(calculator->expression,
             sizeof(calculator->expression),
             "%s %s",
             calculator->display,
             calculator_operator_text(operation));
    return calculator->status;
}

static calculator_status_t calculator_equals(calculator_service_t *calculator)
{
    calculator_operator_t operation;
    double left;
    double right;
    double result;
    char left_text[CALCULATOR_DISPLAY_CAPACITY];
    char right_text[CALCULATOR_DISPLAY_CAPACITY];

    if (calculator->status != CALCULATOR_STATUS_OK)
    {
        return calculator->status;
    }

    if (calculator->pending_operator != CALCULATOR_OPERATOR_NONE)
    {
        operation = calculator->pending_operator;
        left = calculator->accumulator;
        right = calculator->replace_input ? left : calculator_current_value(calculator);
        calculator->last_operator = operation;
        calculator->last_operand = right;
    }
    else if (calculator->result_shown &&
             (calculator->last_operator != CALCULATOR_OPERATOR_NONE))
    {
        operation = calculator->last_operator;
        left = calculator_current_value(calculator);
        right = calculator->last_operand;
    }
    else
    {
        return calculator->status;
    }

    if (!calculator_format_value(left, left_text, sizeof(left_text)) ||
        !calculator_format_value(right, right_text, sizeof(right_text)))
    {
        return calculator_set_error(calculator, CALCULATOR_STATUS_OVERFLOW);
    }

    if (calculator_apply(calculator, operation, left, right, &result) !=
        CALCULATOR_STATUS_OK)
    {
        return calculator->status;
    }

    snprintf(calculator->expression,
             sizeof(calculator->expression),
             "%s %s %s =",
             left_text,
             calculator_operator_text(operation),
             right_text);
    calculator->pending_operator = CALCULATOR_OPERATOR_NONE;
    calculator->replace_input = true;
    calculator->result_shown = true;
    return calculator_set_value(calculator, result);
}

void calculator_service_init(calculator_service_t *calculator)
{
    if (calculator != NULL)
    {
        calculator_reset(calculator);
    }
}

calculator_status_t calculator_service_press(calculator_service_t *calculator,
                                             calculator_key_t key)
{
    calculator_operator_t operation;

    if (calculator == NULL)
    {
        return CALCULATOR_STATUS_INVALID_INPUT;
    }

    if ((key >= CALCULATOR_KEY_0) && (key <= CALCULATOR_KEY_9))
    {
        return calculator_input_digit(calculator, (uint8_t)key);
    }

    operation = calculator_key_to_operator(key);
    if (operation != CALCULATOR_OPERATOR_NONE)
    {
        return calculator_select_operator(calculator, operation);
    }

    switch (key)
    {
        case CALCULATOR_KEY_DECIMAL:
            return calculator_input_decimal(calculator);

        case CALCULATOR_KEY_SIGN:
            return calculator_transform_input(calculator, -1.0);

        case CALCULATOR_KEY_PERCENT:
            return calculator_transform_input(calculator, 0.01);

        case CALCULATOR_KEY_BACKSPACE:
            if (!calculator->replace_input &&
                (calculator->status == CALCULATOR_STATUS_OK))
            {
                size_t length = strlen(calculator->input);
                if (length > 0U)
                {
                    calculator->input[length - 1U] = '\0';
                }
                if ((calculator->input[0] == '\0') ||
                    (strcmp(calculator->input, "-") == 0))
                {
                    strcpy(calculator->input, "0");
                    calculator->replace_input = true;
                }
                strcpy(calculator->display, calculator->input);
            }
            return calculator->status;

        case CALCULATOR_KEY_CLEAR:
            calculator_reset(calculator);
            return calculator->status;

        case CALCULATOR_KEY_EQUALS:
            return calculator_equals(calculator);

        default:
            return calculator_set_error(calculator,
                                        CALCULATOR_STATUS_INVALID_INPUT);
    }
}

const char *calculator_service_get_display(const calculator_service_t *calculator)
{
    return (calculator != NULL) ? calculator->display : "";
}

const char *calculator_service_get_expression(const calculator_service_t *calculator)
{
    return (calculator != NULL) ? calculator->expression : "";
}

calculator_operator_t calculator_service_get_pending_operator(
    const calculator_service_t *calculator)
{
    return (calculator != NULL) ? calculator->pending_operator
                                : CALCULATOR_OPERATOR_NONE;
}
