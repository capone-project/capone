/*
 * Copyright (C) 2016 Patrick Steinhardt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "test.h"

#include "capone/cmdparse.h"
#include "capone/common.h"

static int setup()
{
    return 0;
}

static int teardown()
{
    return 0;
}

static void parsing_nothing_succeeds()
{
    struct cpn_cmdparse_opt opts[] = { CPN_CMDPARSE_OPT_END };
    assert_success(cpn_cmdparse_parse(opts, 0, NULL));
}

static void parsing_with_no_opts_fails()
{
    struct cpn_cmdparse_opt opts[] = { CPN_CMDPARSE_OPT_END };
    const char *args[] = {
        "--test", "value"
    };

    assert_failure(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
}

static void parsing_opt_without_arg_fails()
{
    struct cpn_cmdparse_opt opts[] = { CPN_CMDPARSE_OPT_END };
    const char *args[] = {
        "--test"
    };

    assert_failure(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
}

static void parsing_opt_with_arg_succeeds()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_STRING(0, "--test", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--test", "value"
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_string_equal(opts[0].value.string, "value");
}

static void parsing_opt_without_argument_fails()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_STRING(0, "--test", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--test"
    };

    assert_failure(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
}

static void parsing_with_unset_optional_arg_succeeds()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_STRING(0, "--test", false),
        CPN_CMDPARSE_OPT_STRING(0, "--other", true),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--test", "value"
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_string_equal(opts[0].value.string, "value");
}

static void parsing_multiple_args_succeeds()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_STRING(0, "--test", false),
        CPN_CMDPARSE_OPT_STRING(0, "--other", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--test", "value",
        "--other", "other-value"
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_string_equal(opts[0].value.string, "value");
    assert_string_equal(opts[1].value.string, "other-value");
}

static void parsing_short_arg_succeeds()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_STRING('t', NULL, false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "-t", "value",
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_string_equal(opts[0].value.string, "value");
}

static void parsing_action_succeeds()
{
    static struct cpn_cmdparse_opt action_opts[] = {
        CPN_CMDPARSE_OPT_END
    };
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_ACTION("action", action_opts, false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "action"
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_true(opts[0].set);
}

static void parsing_action_with_additional_args_succeeds()
{
    static struct cpn_cmdparse_opt action_opts[] = {
        CPN_CMDPARSE_OPT_STRING(0, "--test", false),
        CPN_CMDPARSE_OPT_END
    };
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_ACTION("action", action_opts, false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "action",
        "--test", "value"
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_true(opts[0].set);
    assert_string_equal(action_opts[0].value.string, "value");
}

static void parsing_action_with_duplicated_args_succeeds()
{
    static struct cpn_cmdparse_opt action_opts[] = {
        CPN_CMDPARSE_OPT_STRING(0, "--test", false),
        CPN_CMDPARSE_OPT_END
    };
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_STRING(0, "--test", false),
        CPN_CMDPARSE_OPT_ACTION("action", action_opts, false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--test", "general-value",
        "action",
        "--test", "action-value"
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_string_equal(opts[0].value.string, "general-value");
    assert_true(opts[1].set);
    assert_string_equal(action_opts[0].value.string, "action-value");
}

static void parsing_action_with_general_arg_fails()
{
    static struct cpn_cmdparse_opt action_opts[] = {
        CPN_CMDPARSE_OPT_END
    };
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_ACTION("action", action_opts, false),
        CPN_CMDPARSE_OPT_STRING(0, "--test", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "action",
        "--test", "action-value"
    };

    assert_failure(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
}

static void parsing_uint32_succeeds()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_UINT32(0, "--uint32", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--uint32", "12345"
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_int_equal(opts[0].value.uint32, 12345);
}

static void parsing_zero_succeeds()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_UINT32(0, "--uint32", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--uint32", "0"
    };

    assert_success(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
    assert_int_equal(opts[0].value.uint32, 0);
}

static void parsing_uint32_without_argument_fails()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_UINT32(0, "--uint32", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--uint32"
    };

    assert_failure(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
}

static void parsing_negative_fails()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_UINT32(0, "--uint32", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--uint32", "-1"
    };

    assert_failure(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
}

static void parsing_int_with_garbage_fails()
{
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_UINT32(0, "--uint32", false),
        CPN_CMDPARSE_OPT_END
    };
    const char *args[] = {
        "--uint32", "12345garbage"
    };

    assert_failure(cpn_cmdparse_parse(opts, ARRAY_SIZE(args), args));
}

int cmdparse_test_run_suite(void)
{
    const struct CMUnitTest tests[] = {
        test(parsing_nothing_succeeds),
        test(parsing_with_no_opts_fails),
        test(parsing_opt_without_arg_fails),

        test(parsing_opt_with_arg_succeeds),
        test(parsing_opt_without_argument_fails),
        test(parsing_with_unset_optional_arg_succeeds),
        test(parsing_multiple_args_succeeds),
        test(parsing_short_arg_succeeds),

        test(parsing_action_succeeds),
        test(parsing_action_with_additional_args_succeeds),
        test(parsing_action_with_duplicated_args_succeeds),
        test(parsing_action_with_general_arg_fails),

        test(parsing_uint32_succeeds),
        test(parsing_zero_succeeds),
        test(parsing_uint32_without_argument_fails),
        test(parsing_negative_fails),
        test(parsing_int_with_garbage_fails)
    };

    return execute_test_suite("cmdparse", tests, setup, teardown);
}
