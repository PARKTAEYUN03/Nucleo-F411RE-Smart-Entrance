#include "mbed.h"

// =========================================================================
// 1. 하드웨어 핀 및 센서 객체 정의 
// =========================================================================
DigitalOut trig(D9);       // 초음파 센서 송신 트리거 핀 (D9)
DigitalIn  echo(D8);       // 초음파 센서 수신 에코 핀 (D8)
AnalogIn   cds(A0);        // 조도 센서 아날로그 입력 핀 (A0)
DigitalIn  pir(D10);       // PIR 움직임 감지 센서 입력 핀 (D10)
DigitalIn  button(D11);    // 선 2개로만 연결한 외부 수동 푸시 버튼 핀 (D11)

PwmOut     green_led(D3);  // 조명 1: 어두움에 연동하여 밝기가 조절되는 초록색 LED (D3) 
DigitalOut blue_led(D2);   // 조명 2: 버튼 입력을 통해 ON/OFF 토글되는 파란색 LED (D2) 
DigitalOut red_led(D5);    // 경고등: 움직임 감지 시 깜박이는 빨간색 LED (D5) 
DigitalOut buzzer(D4);     // 경보음: 제한거리 이내 접근 시 울리는 부저 (D4) 

// =========================================================================
// 2. 동시 작동(Non-blocking) 제어를 위한 전역 타이머 및 변수 설정 
// =========================================================================
Timer system_timer;        // 밀리초(ms) 단위의 시스템 시계를 생성하기 위한 타이머 객체

int last_ultrasonic_time = 0; // 초음파 거리 측정 주기를 관리하는 시간 저장 변수
int last_red_led_time = 0;    // 빨간색 LED 깜박임 주기를 관리하는 시간 저장 변수
int last_button_time = 0;     // 버튼 디바운싱(채터링 오작동 방지) 주기를 관리하는 변수
int last_buzzer_time = 0;     // 부저 경고음 주파수(톤) 생성을 위한 시간 저장 변수

bool distance_alarm = false;  // 30cm 이내 접근 경보 활성화 여부 플래그

int main() {
    // 내부 풀업 저항 활성화 (외부 저항 없이 점퍼선 2개로 버튼을 구현하기 위함)
    button.mode(PullUp);
    
    // PWM 주기 설정 (초록색 LED의 밝기를 부드럽게 제어하기 위해 0.001초 주기로 설정)
    green_led.period(0.001f); 
    
    // 시스템 메인 타이머 가동 시작
    system_timer.start();

    while(1) {
        // 4개 기능의 동시 작동을 위해 현재 누적 경과 시간(ms)을 실시간으로 갱신
        int current_time = system_timer.read_ms();

        // =========================================================================
        // 기능 1: 접근 경보 시스템 (초음파 거리측정 및 부저 제어) 
        // =========================================================================
        // 메인 루프를 멈추지 않기 위해 100ms마다 주기적으로 초음파를 쏴서 거리 계산
        if (current_time - last_ultrasonic_time >= 100) {
            last_ultrasonic_time = current_time;

            // Trig 핀으로 10마이크로초(us) 동안 펄스를 발생시켜 초음파 발사 트리거
            trig = 1;
            wait_us(10);
            trig = 0;

            // Echo 핀이 HIGH로 바뀔 때까지 하드웨어 상태를 스캔한 후 시간 측정 시작
            Timer echo_timer;
            while(echo == 0); 
            echo_timer.start();
            while(echo == 1); // Echo 핀이 LOW로 떨어질 때까지 대기
            echo_timer.stop();

            // 측정된 us 단위 시간을 cm 거리 데이터로 변환 (음속 공식 적용)
            float distance = echo_timer.read_us() / 58.0f;

            // 과제 요구사항: 제한거리(30cm 이내) 안으로 물체가 감지되었는지 확인 
            if (distance < 30.0f && distance > 0.0f) {
                distance_alarm = true; // 접근 경보 플래그 활성화
            } else {
                distance_alarm = false; // 정상 상태 해제
                buzzer = 0;             // 부저 정지
            }
        }

        // 접근 경보 상태일 때, 시스템 지연(wait) 없이 특정 주파수로 부저를 울림 
        if (distance_alarm) {
            if (current_time - last_buzzer_time >= 2) { // 2ms 주기로 부저 핀을 토글하여 경고음 구현
                last_buzzer_time = current_time;
                buzzer = !buzzer; 
            }
        }

        // =========================================================================
        // 기능 2: 물체움직임 경보 시스템 (PIR 센서 및 빨간색 LED) 
        // =========================================================================
        // PIR 센서가 움직임을 포착(HIGH)하면 빨간색 LED를 비블로킹 방식으로 깜박임 
        if (pir == 1) {
            if (current_time - last_red_led_time >= 250) { // 250ms 간격으로 깜박임 제어
                last_red_led_time = current_time;
                red_led = !red_led;
            }
        } else {
            red_led = 0; // 움직임이 사라지면 경고등 즉시 소등
        }

        // =========================================================================
        // 기능 3: 조명제어1 (조도 센서에 따른 초록색 LED PWM 밝기 제어) 
        // =========================================================================
        // AnalogIn API는 내부 ADC를 통해 0.0(완전어두움) ~ 1.0(완전밝음) 사이 값을 반환함 
        float light_val = cds.read();
        
        // 요구사항 반영: 어두우면 켜지고 밝으면 꺼져야 하므로 수식을 반전매핑 처리함 
        float pwm_val = 1.0f - light_val;
        
        // 완전히 밝은 주간 환경(임계값 조도 0.7 이상)에서는 조명을 강제로 차단 소등함 
        if (light_val > 0.7f) {
            pwm_val = 0.0f;
        }
        
        // 계산된 듀티 비(Duty Cycle)를 초록색 LED에 융합 출력하여 자동 조광 구현
        green_led = pwm_val;

        // =========================================================================
        // 기능 4: 조명제어2 (외부 버튼 입력을 통한 파란색 LED 토글 제어) 
        // =========================================================================
        // 내부 풀업 저항 설정으로 인해 버튼을 누르면 평소 1에서 0(LOW) 신호로 떨어짐
        if (button == 0) {
            // 기계적 떨림(채터링 현상)으로 조명이 난사되는 것을 막기 위한 300ms 소프트웨어 디바운싱
            if (current_time - last_button_time >= 300) {
                last_button_time = current_time;
                blue_led = !blue_led; // 파란색 LED 상태反轉 (ON/OFF 토글) 
            }
        }
    }
}