################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Rte/Rte.c 

OBJS += \
./Core/Rte/Rte.o 

C_DEPS += \
./Core/Rte/Rte.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Rte/%.o Core/Rte/%.su Core/Rte/%.cyclo: ../Core/Rte/%.c Core/Rte/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F030x6 -c -I../Core/Inc -I../Drivers/STM32F0xx_HAL_Driver/Inc -I../Drivers/STM32F0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F0xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/Admin/OneDrive/Desktop/hieuvm/14. Charger_Protection_v2/Charger_Protection_v2/software/Charger_Protection_v2/Core/Rte" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Rte

clean-Core-2f-Rte:
	-$(RM) ./Core/Rte/Rte.cyclo ./Core/Rte/Rte.d ./Core/Rte/Rte.o ./Core/Rte/Rte.su

.PHONY: clean-Core-2f-Rte

