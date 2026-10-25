################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Calib/Calib.c \
../Core/Calib/CalibFlash.c 

OBJS += \
./Core/Calib/Calib.o \
./Core/Calib/CalibFlash.o 

C_DEPS += \
./Core/Calib/Calib.d \
./Core/Calib/CalibFlash.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Calib/%.o Core/Calib/%.su Core/Calib/%.cyclo: ../Core/Calib/%.c Core/Calib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0 -std=gnu11 -DSTM32F030x6 -DUSE_FULL_LL_DRIVER -DHSE_VALUE=8000000 -DHSE_STARTUP_TIMEOUT=100 -DLSE_STARTUP_TIMEOUT=5000 -DLSE_VALUE=32768 -DHSI_VALUE=8000000 -DLSI_VALUE=40000 -DVDD_VALUE=3300 -DPREFETCH_ENABLE=1 -DINSTRUCTION_CACHE_ENABLE=0 -DDATA_CACHE_ENABLE=0 -c -I../Core/Measurement -I../Core/Rte -I../Core/Protection -I../Core/Calib -I../Core/Com -I../Core/Inc -I../Drivers/STM32F0xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F0xx/Include -I../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -flto -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Calib

clean-Core-2f-Calib:
	-$(RM) ./Core/Calib/Calib.cyclo ./Core/Calib/Calib.d ./Core/Calib/Calib.o ./Core/Calib/Calib.su ./Core/Calib/CalibFlash.cyclo ./Core/Calib/CalibFlash.d ./Core/Calib/CalibFlash.o ./Core/Calib/CalibFlash.su

.PHONY: clean-Core-2f-Calib

