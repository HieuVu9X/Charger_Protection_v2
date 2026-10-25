################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Rte/Rte.c \
../Core/Rte/scheduler.c 

OBJS += \
./Core/Rte/Rte.o \
./Core/Rte/scheduler.o 

C_DEPS += \
./Core/Rte/Rte.d \
./Core/Rte/scheduler.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Rte/%.o Core/Rte/%.su Core/Rte/%.cyclo: ../Core/Rte/%.c Core/Rte/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0 -std=gnu11 -g3 -DDEBUG -DSTM32F030x6 -DUSE_FULL_LL_DRIVER -DHSE_VALUE=8000000 -DHSE_STARTUP_TIMEOUT=100 -DLSE_STARTUP_TIMEOUT=5000 -DLSE_VALUE=32768 -DHSI_VALUE=8000000 -DLSI_VALUE=40000 -DVDD_VALUE=3300 -DPREFETCH_ENABLE=1 -DINSTRUCTION_CACHE_ENABLE=0 -DDATA_CACHE_ENABLE=0 -c -I../Core/Inc -I../Drivers/STM32F0xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F0xx/Include -I../Drivers/CMSIS/Include -I../Core/Measurement -I../Core/Rte -I../Core/Protection -I../Core/Calib -I../Core/Com -Os -ffunction-sections -fdata-sections -Wall -flto -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Rte

clean-Core-2f-Rte:
	-$(RM) ./Core/Rte/Rte.cyclo ./Core/Rte/Rte.d ./Core/Rte/Rte.o ./Core/Rte/Rte.su ./Core/Rte/scheduler.cyclo ./Core/Rte/scheduler.d ./Core/Rte/scheduler.o ./Core/Rte/scheduler.su

.PHONY: clean-Core-2f-Rte

