#include "Status.h"
#include<stdint.h>

bit workingStatus = 0;  // 0 - idle, 1 - working
uint8_t effic[5];
uint8_t volt[5];
float loadEff[36];
float Req[36];
uint16_t elapsedTime = 0;




/*状态管理函数*/
void setWorkingStatus(bit status) {
    workingStatus = status;
}

void setEfficencyData(uint8_t *data) {
    for (uint8_t i = 0; i < 4; i++) {
        effic[i] = data[i];
    }
    effic[4] = '\0';
}

void setVoltageData(uint8_t *data) {
    for (uint8_t i = 0; i < 4; i++) {
        volt[i] = data[i];
    }
    volt[4] = '\0';
}

void setLoadEfficiency(float *data, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        loadEff[i] = data[i];
    }
}

void setEquivalentResistance(float *data, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        Req[i] = data[i];
    }
}

void setElapsedTime(uint16_t time) {
    elapsedTime = time;
}

/*状态获取函数*/

bit getWorkingStatus(void) {
    return workingStatus;
}

uint8_t* getEfficencyData(void) {
    return effic;
}

uint8_t* getVoltageData(void) {
    return volt;
}

float* getLoadEfficiency(void) {
    return loadEff;
}

float* getEquivalentResistance(void) {
    return Req;
}

uint16_t getElapsedTime(void) {
    return elapsedTime;
}