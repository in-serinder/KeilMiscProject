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

print("并联阻值数组 (float):")
print(resistance_list)
print("\n十六进制控制字数组:")
print(hex_list)
print("\n并联电阻组合数组:")
print(r_group)
