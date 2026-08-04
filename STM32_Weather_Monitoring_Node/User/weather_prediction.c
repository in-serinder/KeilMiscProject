#include "weather_prediction.h"
#include <string.h>

/*
 * 气象预测算法(基于长期大气压变化趋势)
 * ------------------------------------------------------------------
 * 原理: 气压变化是天气系统移动最可靠的先兆之一(气象学经验, 海平面气压 hPa)
 *   - 气压骤降(10min下降>=1.0hPa)      -> 强对流天气逼近: 转雷暴/强对流
 *   - 气压持续下降(10min 0.3~1.0hPa,
 *     或60min下降>=1.5hPa)             -> 低压槽过境: 转降雨/刮风
 *   - 气压回升(10min>=0.5hPa 或
 *     60min>=1.5hPa)                   -> 高压脊控制: 转晴天
 *   - 气压平稳                          -> 天气系统稳定: 转阴天
 *
 * 数据流: main.c 每 60s(TASK_WEATHER_PRED_TICKS)调用一次 Weather_Prediction_Update()
 *        每次采样当前气压存入环形缓冲, 用"短窗口(10样本)+长窗口(60样本)"差分
 *        得到趋势; 候选状态需连续 WP_CONFIRM_NUM 次(3分钟)相同才确认输出,
 *        与 main.c 的10分钟蜂鸣冷却共同解决判断抖动问题.
 * ------------------------------------------------------------------
 */

#define WP_HISTORY_MAX 64       //历史气压环形缓冲: 64样本 = 64分钟 
#define WP_CONFIRM_NUM 3        //状态稳定确认样本数(3样本 = 3分钟)
#define WP_TREND_WIN_SHORT 10   //短期趋势窗口: 10样本 = 10分钟
#define WP_TREND_WIN_LONG 60    //长期趋势窗口: 60样本 = 60分钟

/* 气压趋势阈值(hPa) */
#define WP_DROP_FAST_SHORT (-1.0f) //10min降幅>=1.0hPa -> 雷暴强对流
#define WP_DROP_SLOW_SHORT (-0.3f) //10min降幅0.3~1.0hPa -> 降雨
#define WP_DROP_LONG       (-1.5f) //60min降幅>=1.5hPa  -> 降雨
#define WP_RISE_SHORT      (+0.5f) //10min升幅>=0.5hPa  -> 转晴
#define WP_RISE_LONG       (+1.5f) //60min升幅>=1.5hPa  -> 转晴

static float pressHist[WP_HISTORY_MAX]; //气压环形缓冲
static uint8_t pressCnt = 0;            //已采样样本数
static uint8_t pressIdx = 0;            //环形写指针(指向下次写入位置)

static Weather_Prediction wpCurrent = Weather_Prediction_Invalid;   //已确认输出
static Weather_Prediction wpCandidate = Weather_Prediction_Invalid; //候选状态
static uint8_t wpConfirmCnt = 0;        //候选连续相同计数

// 取 back 个样本之前的气压值(环形缓冲反向索引) 
static float History_Get(uint8_t back) {
  int16_t i = (int16_t)pressIdx - (int16_t)back;
  if (i < 0)
    i += WP_HISTORY_MAX;
  return pressHist[i];
}

void Weather_Prediction_Init(void) {
  memset(pressHist, 0, sizeof(pressHist));
  pressCnt = 0;
  pressIdx = 0;
  wpCurrent = Weather_Prediction_Invalid;
  wpCandidate = Weather_Prediction_Invalid;
  wpConfirmCnt = 0;
}

void Weather_Prediction_Update(void) {
  float p, dPShort, dPLong = 0.0f;
  Weather_Prediction cand;

  
  BMP280_Sensor_Read();
  p = BMP280_Sensor_ReadPressure();

  // 存入环形缓冲
  pressHist[pressIdx] = p;
  pressIdx = (pressIdx + 1) % WP_HISTORY_MAX;
  if (pressCnt < WP_HISTORY_MAX)
    pressCnt++;
  
  // 样本不足(未积累够短期窗口) -> 预测无效
  if (pressCnt < WP_TREND_WIN_SHORT) {
    wpCandidate = Weather_Prediction_Invalid;
    wpConfirmCnt = 0;
    wpCurrent = Weather_Prediction_Invalid;
    return;
  }
  
  // 趋势计算: 当前气压 - 窗口起点气压
  dPShort = p - History_Get(WP_TREND_WIN_SHORT); // 10分钟变化
  if (pressCnt >= WP_TREND_WIN_LONG)
    dPLong = p - History_Get(WP_TREND_WIN_LONG); // 60分钟变化
  
  // 气压趋势 -> 候选气象状态
  if (dPShort <= WP_DROP_FAST_SHORT) {
    cand = Weather_Prediction_ToThunder; // 气压骤降: 雷暴/强对流
  } else if (dPShort <= WP_DROP_SLOW_SHORT || dPLong <= WP_DROP_LONG) {
    cand = Weather_Prediction_ToRainy; // 持续下降: 降雨/刮风
  } else if (dPShort >= WP_RISE_SHORT || dPLong >= WP_RISE_LONG) {
    cand = Weather_Prediction_ToSunny; // 气压回升: 转晴
  } else {
    cand = Weather_Prediction_ToOvercast; // 平稳: 阴天
  }

  /* 防抖确认: 候选需连续 WP_CONFIRM_NUM 次(3分钟)相同才提交,
     防止单次波动/传感器噪声导致状态抖动 */
  if (cand == wpCandidate) {
    if (wpConfirmCnt < WP_CONFIRM_NUM)
      wpConfirmCnt++;
    if (wpConfirmCnt >= WP_CONFIRM_NUM)
      wpCurrent = wpCandidate;
  } else {
    wpCandidate = cand;
    wpConfirmCnt = 1;
  }
}


Weather_Prediction Weather_Prediction_Print(void) {
  return wpCurrent;
}
