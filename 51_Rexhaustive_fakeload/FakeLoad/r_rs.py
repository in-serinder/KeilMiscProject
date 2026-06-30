# 电阻并联穷举
# 定义引脚对应的电阻值 (p0~p5)
resistors = {
    0: 10,   # P0: 10Ω
    1: 20,   # P1: 20Ω
    2: 20,   # P2: 20Ω
    3: 20,   # P3: 20Ω
    4: 20,   # P4: 20Ω
    5: 100   # P5: 100Ω
}

power_tolerance = {
    0: 10,   # P0: 10Ω 10w
    1: 20,   # P1: 20Ω 20w
    2: 20,   # P2: 20Ω 20w
    3: 20,   # P3: 20Ω 20w
    4: 20,   # P4: 20Ω 20w
    5: 5   # P5: 100Ω 5w
}

simulation_voltage = 12.0  # 12V 电压
voltage_coefficient = 0.8  # 电压系数，voltage * coefficient = 100 时视为超限临界值
                          # 系数越小，越容易通过（保留更多组合）
                          # 系数越大，越严格（筛选更严格）


resistance_list = []  
hex_list = []     
r_group = []   

# 遍历 0~63 所有组合
for i in range(64):
    sum_reciprocal = 0.0
    group = ""
    # 计算并联电阻
    for bit, r in resistors.items():
        if not (i & (1 << bit)):
            sum_reciprocal += 1.0 / r
            group += 'P{0}: {1}Ω,'.format(bit, r)
    if sum_reciprocal == 0:
        r_total = float('inf')
    else:
        r_total = 1.0 / sum_reciprocal
    r_group.append(group)
    resistance_list.append(round(r_total, 2))
    hex_val = 0xC0 | i
    hex_list.append("0x%02X" % hex_val)

print(f"并联阻值数组 (float): len={len(resistance_list)}")
print(resistance_list)
print(f"\n十六进制控制字数组: len={len(hex_list)}")
print(hex_list)
print(f"\n并联电阻组合数组: len={len(r_group)}")
print(r_group)

# 计算每个功率组的耐受功耗（标准模式）
print("\n" + "="*60)
print(f"【标准模式】功耗耐受分析 (模拟电压: {simulation_voltage}V)")
print("="*60)
print(f"{'索引':^6} {'组合':^30} {'总电阻':^10} {'总功率':^10} {'状态':^10}")
print("-"*60)

safe_combinations = []
unsafe_combinations = []

for i in range(64):
    r_total = resistance_list[i]
    group = r_group[i]
    
    if r_total == float('inf'):
        total_power = 0.0
        status = "未使用"
    else:
        # 计算总功率 P = V² / R
        total_power = round(simulation_voltage ** 2 / r_total, 2)
        
        # 检查每个启用电阻的功耗是否超过耐受度
        is_safe = True
        for bit, r in resistors.items():
            if not (i & (1 << bit)):
                # 该电阻被启用，计算其功耗
                # 并联电路中各支路电压相同
                resistor_power = round(simulation_voltage ** 2 / r, 2)
                tolerance = power_tolerance[bit]
                if resistor_power > tolerance:
                    is_safe = False
                    break
        
        status = "安全" if is_safe else "超限"
        
        if is_safe:
            safe_combinations.append((i, r_total, total_power, group))
        else:
            unsafe_combinations.append((i, r_total, total_power, group))
    
    print(f"{i:^6} {group[:28]:<30} {r_total:^10.2f} {total_power:^10.2f} {status:^10}")

print("\n" + "="*60)
print(f"安全组合: {len(safe_combinations)} 个")
print(f"超限组合: {len(unsafe_combinations)} 个")

# 输出安全组合的详细信息
if safe_combinations:
    print("\n安全组合详情:")
    for idx, r, p, group in safe_combinations:
        print(f"  [{idx}] {group} -> {r:.2f}Ω, {p:.2f}W")

# ==================== 系数模式分析 ====================
# 使用电压系数计算，保留更多小电阻组合
print("\n" + "="*70)
print(f"【系数模式】功耗耐受分析")
print(f"  模拟电压: {simulation_voltage}V, 电压系数: {voltage_coefficient}")
print(f"  等效判断电压: {simulation_voltage * voltage_coefficient:.2f}V")
print(f"  临界条件: voltage * coefficient = 100V 时视为完全超限")
print("="*70)
print(f"{'索引':^6} {'组合':^30} {'总电阻':^10} {'等效功率':^12} {'状态':^10} {'裕量':^10}")
print("-"*70)

coeff_safe_combinations = []
coeff_unsafe_combinations = []

for i in range(64):
    r_total = resistance_list[i]
    group = r_group[i]
    
    if r_total == float('inf'):
        total_power = 0.0
        status = "未使用"
        margin = "N/A"
    else:
        # 使用系数后的等效电压计算功耗
        effective_voltage = simulation_voltage * voltage_coefficient
        total_power = round(effective_voltage ** 2 / r_total, 2)
        
        # 检查每个启用电阻的功耗是否超过耐受度
        is_safe = True
        max_ratio = 0.0
        for bit, r in resistors.items():
            if not (i & (1 << bit)):
                # 该电阻被启用，计算其功耗
                resistor_power = round(effective_voltage ** 2 / r, 2)
                tolerance = power_tolerance[bit]
                ratio = resistor_power / tolerance
                max_ratio = max(max_ratio, ratio)
                if resistor_power > tolerance:
                    is_safe = False
        
        if is_safe:
            status = "安全"
            margin = f"{((1 - max_ratio) * 100):.0f}%"
        else:
            status = "超限"
            margin = f"-{((max_ratio - 1) * 100):.0f}%"
        
        if is_safe:
            coeff_safe_combinations.append((i, r_total, total_power, group))
        else:
            coeff_unsafe_combinations.append((i, r_total, total_power, group))
    
    print(f"{i:^6} {group[:28]:<30} {r_total:^10.2f} {total_power:^12.2f} {status:^10} {margin:^10}")

print("\n" + "="*70)
print(f"标准模式 安全组合: {len(safe_combinations)} 个")
print(f"系数模式 安全组合: {len(coeff_safe_combinations)} 个")
print(f"差异: {len(coeff_safe_combinations) - len(safe_combinations)} 个组合被保留")

# 输出系数模式下额外保留的组合
if len(coeff_safe_combinations) > len(safe_combinations):
    standard_indices = {item[0] for item in safe_combinations}
    additional_combinations = [item for item in coeff_safe_combinations if item[0] not in standard_indices]
    print("\n系数模式额外保留的组合:")
    for idx, r, p, group in additional_combinations:
        print(f"  [{idx}] {group} -> {r:.2f}Ω, {p:.2f}W")