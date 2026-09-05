#
# SPDX-License-Identifier: BSD-2-Clause
#

carray-platform_override_modules-$(CONFIG_PLATFORM_ALLWINNER_D1) += sun20i_d1
platform-objs-$(CONFIG_PLATFORM_ALLWINNER_D1) += allwinner/sun20i-d1.o

carray-platform_override_modules-$(CONFIG_PLATFORM_ALLWINNER_SUN252I_F101) += sun252i_f101
platform-objs-$(CONFIG_PLATFORM_ALLWINNER_SUN252I_F101) += allwinner/sun252i-f101.o
