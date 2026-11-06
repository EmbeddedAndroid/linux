// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the PM clock layer.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/pm_clock.h>

#include <kunit/clk.h>
#include <kunit/device.h>
#include <kunit/test.h>

/*
 * A clock that counts its prepare and enable calls. Without enable and
 * disable operations it behaves like a vote clock that is enabled once
 * prepared, which the PM clock layer only prepares on resume.
 */
struct pm_clk_test_clk {
	struct clk_hw hw;
	int prepare_ret;
	int prepare_count;
	int enable_count;
};

#define to_pm_clk_test_clk(_hw) container_of(_hw, struct pm_clk_test_clk, hw)

static int pm_clk_test_prepare(struct clk_hw *hw)
{
	struct pm_clk_test_clk *tclk = to_pm_clk_test_clk(hw);

	if (tclk->prepare_ret)
		return tclk->prepare_ret;

	tclk->prepare_count++;

	return 0;
}

static void pm_clk_test_unprepare(struct clk_hw *hw)
{
	to_pm_clk_test_clk(hw)->prepare_count--;
}

static int pm_clk_test_enable(struct clk_hw *hw)
{
	to_pm_clk_test_clk(hw)->enable_count++;

	return 0;
}

static void pm_clk_test_disable(struct clk_hw *hw)
{
	to_pm_clk_test_clk(hw)->enable_count--;
}

static const struct clk_ops pm_clk_test_gate_ops = {
	.prepare = pm_clk_test_prepare,
	.unprepare = pm_clk_test_unprepare,
	.enable = pm_clk_test_enable,
	.disable = pm_clk_test_disable,
};

static const struct clk_ops pm_clk_test_vote_ops = {
	.prepare = pm_clk_test_prepare,
	.unprepare = pm_clk_test_unprepare,
};

struct pm_clk_test_ctx {
	struct device *dev;
	struct pm_clk_test_clk gate;
	struct pm_clk_test_clk vote;
};

static void pm_clk_test_register_clk(struct kunit *test,
				     struct pm_clk_test_clk *tclk,
				     const char *name,
				     const struct clk_ops *ops)
{
	tclk->hw.init = CLK_HW_INIT_NO_PARENT(name, ops, 0);
	KUNIT_ASSERT_EQ(test, clk_hw_register_kunit(test, NULL, &tclk->hw), 0);
}

static void pm_clk_test_add_clk(struct kunit *test, struct device *dev,
				struct pm_clk_test_clk *tclk)
{
	struct clk *clk;

	/* The PM clock layer owns the reference and puts it on destroy. */
	clk = clk_hw_get_clk(&tclk->hw, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, clk);
	KUNIT_ASSERT_EQ(test, pm_clk_add_clk(dev, clk), 0);
}

static int pm_clk_test_init(struct kunit *test)
{
	struct pm_clk_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	test->priv = ctx;

	/* Register the clocks first so that they outlive the PM clock list. */
	pm_clk_test_register_clk(test, &ctx->gate, "pm-clk-test-gate",
				 &pm_clk_test_gate_ops);
	pm_clk_test_register_clk(test, &ctx->vote, "pm-clk-test-vote",
				 &pm_clk_test_vote_ops);

	ctx->dev = kunit_device_register(test, "pm-clk-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->dev);
	KUNIT_ASSERT_EQ(test, devm_pm_clk_create(ctx->dev), 0);

	pm_clk_test_add_clk(test, ctx->dev, &ctx->gate);
	pm_clk_test_add_clk(test, ctx->dev, &ctx->vote);

	return 0;
}

/* Resume enables every clock and suspend turns them all off again. */
static void pm_clk_test_resume_suspend(struct kunit *test)
{
	struct pm_clk_test_ctx *ctx = test->priv;

	KUNIT_ASSERT_EQ(test, pm_clk_resume(ctx->dev), 0);
	KUNIT_EXPECT_EQ(test, ctx->gate.enable_count, 1);
	KUNIT_EXPECT_EQ(test, ctx->vote.prepare_count, 1);

	KUNIT_ASSERT_EQ(test, pm_clk_suspend(ctx->dev), 0);
	KUNIT_EXPECT_EQ(test, ctx->gate.enable_count, 0);
	KUNIT_EXPECT_EQ(test, ctx->vote.prepare_count, 0);
}

/*
 * A clock that fails to enable fails the resume, and the clocks enabled
 * before it are turned off again.
 */
static void pm_clk_test_resume_error(struct kunit *test)
{
	struct pm_clk_test_ctx *ctx = test->priv;

	ctx->vote.prepare_ret = -ENXIO;

	KUNIT_EXPECT_EQ(test, pm_clk_resume(ctx->dev), -ENXIO);
	KUNIT_EXPECT_EQ(test, ctx->gate.enable_count, 0);
	KUNIT_EXPECT_EQ(test, ctx->vote.prepare_count, 0);
}

/* A failed resume leaves the clock list in a state that a retry can use. */
static void pm_clk_test_resume_retry(struct kunit *test)
{
	struct pm_clk_test_ctx *ctx = test->priv;

	ctx->vote.prepare_ret = -ENXIO;
	KUNIT_ASSERT_EQ(test, pm_clk_resume(ctx->dev), -ENXIO);

	ctx->vote.prepare_ret = 0;
	KUNIT_ASSERT_EQ(test, pm_clk_resume(ctx->dev), 0);
	KUNIT_EXPECT_EQ(test, ctx->gate.enable_count, 1);
	KUNIT_EXPECT_EQ(test, ctx->vote.prepare_count, 1);

	KUNIT_ASSERT_EQ(test, pm_clk_suspend(ctx->dev), 0);
	KUNIT_EXPECT_EQ(test, ctx->gate.enable_count, 0);
	KUNIT_EXPECT_EQ(test, ctx->vote.prepare_count, 0);
}

static struct kunit_case pm_clk_test_cases[] = {
	KUNIT_CASE(pm_clk_test_resume_suspend),
	KUNIT_CASE(pm_clk_test_resume_error),
	KUNIT_CASE(pm_clk_test_resume_retry),
	{ }
};

static struct kunit_suite pm_clk_test_suite = {
	.name = "pm-clk",
	.init = pm_clk_test_init,
	.test_cases = pm_clk_test_cases,
};

kunit_test_suite(pm_clk_test_suite);

MODULE_DESCRIPTION("KUnit tests for the PM clock layer");
MODULE_LICENSE("GPL");
