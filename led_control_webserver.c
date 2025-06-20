#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

// Configurações de Wi-Fi
#define WIFI_SSID "your_ssid_here"
#define WIFI_PASSWORD "your_password_here"

// Definição dos pinos dos LEDs
#define LED_PIN CYW43_WL_GPIO_LED_PIN
#define LED_BLUE_PIN 12
#define LED_GREEN_PIN 11
#define LED_RED_PIN 13

// Pinos do joystick
#define JOY_X_PIN 26 // GPIO26 = ADC0
#define JOY_Y_PIN 27 // GPIO27 = ADC1
#define JOY_X_ADC 0
#define JOY_Y_ADC 1

// Variáveis globais para armazenar as posições
static uint16_t joystick_x = 0;
static uint16_t joystick_y = 0;

// Protótipo da função de direção
static const char* get_joystick_direction(uint16_t x, uint16_t y);

// Função para ler o valor médio do ADC
static uint16_t read_adc_avg(uint adc_input) {
    adc_select_input(adc_input);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += adc_read();
        sleep_us(100);
    }
    return sum / 8;
}

// Função para determinar a direção do joystick (rosa dos ventos)
static const char* get_joystick_direction(uint16_t x, uint16_t y) {
    // Ajuste os limites conforme necessário para seu joystick
    const uint16_t center_min = 1800;
    const uint16_t center_max = 2200;
    if (x >= center_min && x <= center_max && y >= center_min && y <= center_max)
        return "Centro";

    if (y > center_max) {
        if (x > center_max) return "Nordeste";
        if (x < center_min) return "Noroeste";
        return "Leste";
    }
    if (y < center_min) {
        if (x > center_max) return "Sudeste";
        if (x < center_min) return "Sudoeste";
        return "Oeste";
    }
    if (x > center_max) return "Norte";
    if (x < center_min) return "Sul";
    return "Centro";
}

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    const char* dir = get_joystick_direction(joystick_x, joystick_y);

    char html[1024];
    snprintf(html, sizeof(html),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Refresh: 1\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Joystick BitDogLab Monitor</title></head>\n"
        "<body style='font-family:Arial;text-align:center;margin-top:50px;'>\n"
        "<h1>Joystick Position</h1>\n"
        "<p style='font-size:2em;'>X: <b>%d</b></p>\n"
        "<p style='font-size:2em;'>Y: <b>%d</b></p>\n"
        "<p style='font-size:2em;'>Direction: <b>%s</b></p>\n"
        "<p style='color:gray;'>The page refreshes every second.</p>\n"
        "</body>\n"
        "</html>\n",
        joystick_x, joystick_y, dir);

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

int main()
{
    stdio_init_all();

    // Inicializa LEDs (opcional)
    gpio_init(LED_BLUE_PIN); gpio_set_dir(LED_BLUE_PIN, GPIO_OUT); gpio_put(LED_BLUE_PIN, false);
    gpio_init(LED_GREEN_PIN); gpio_set_dir(LED_GREEN_PIN, GPIO_OUT); gpio_put(LED_GREEN_PIN, false);
    gpio_init(LED_RED_PIN); gpio_set_dir(LED_RED_PIN, GPIO_OUT); gpio_put(LED_RED_PIN, false);

    // Inicializa Wi-Fi
    while (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    cyw43_arch_gpio_put(LED_PIN, 0);
    cyw43_arch_enable_sta_mode();

    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Aguarda até o IP ser atribuído e imprime uma única vez
    while (!netif_default || netif_default->ip_addr.addr == 0) {
        sleep_ms(100);
    }
    printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));

    // Configura o servidor TCP
    struct tcp_pcb *server = tcp_new();
    if (!server) {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }
    server = tcp_listen(server);
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");

    // Inicializa o ADC e os pinos do joystick
    adc_init();
    adc_gpio_init(JOY_X_PIN);
    adc_gpio_init(JOY_Y_PIN);

    uint32_t last_read = 0;
    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_read > 200) { // lê a cada 200ms
            joystick_x = read_adc_avg(JOY_X_ADC);
            joystick_y = read_adc_avg(JOY_Y_ADC);
            const char* dir = get_joystick_direction(joystick_x, joystick_y);
            printf("Joystick X: %d | Y: %d | Direção: %s\n", joystick_x, joystick_y, dir);
            last_read = now;
        }
        cyw43_arch_poll();
        sleep_ms(10);
    }

    cyw43_arch_deinit();
    return 0;
}
