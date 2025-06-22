#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h" // Para acessar netif_default e IP

// Configurações de Wi-Fi
#define WIFI_SSID "your_ssid_here"
#define WIFI_PASSWORD "your_password_here"

// Definição dos pinos dos LEDs
#define LED_PIN CYW43_WL_GPIO_LED_PIN
#define LED_BLUE_PIN 12  // GPIO12 - LED azul
#define LED_GREEN_PIN 11 // GPIO11 - LED verde
#define LED_RED_PIN 13   // GPIO13 - LED vermelho

#define BUTTON_B_PIN 6
#define BUTTON_A_PIN 5
#define POLL_INTERVAL_MS 1000  // Intervalo de 1 segundo

// Definições para o sensor de som
#define SOUND_SENSOR_PIN 28    // ADC2
#define SOUND_ADC_INPUT 2      // Canal ADC2
#define SOUND_SAMPLES 32       // Amostras para média
#define SOUND_THRESHOLD 50     // Limiar de detecção
#define MIN_REPORT_TIME 200    // Tempo mínimo entre relatórios (ms)
#define FREQ_MIN 85           
#define FREQ_MAX 800          
#define NOISE_THRESHOLD 100    
#define ADC_SAMPLES 32        

// Variáveis globais necessárias
static bool last_button_state = true;
static bool last_button_a_state = true;
static uint16_t min_adc = UINT16_MAX;
static uint16_t max_adc = 0;
static uint32_t sample_count = 0;
static uint16_t baseline_adc = 0;
static uint16_t current_sound_level = 0;

// Adicionar estas variáveis globais
static char last_sound_level[10] = "NONE";    // Para armazenar o último nível de som
static int16_t last_sound_variation = 0;      // Última variação de som
static uint16_t last_sound_value = 0;         // Último valor ADC do som
static uint32_t global_time_ms = 0;  // Contador global de tempo

// Estrutura para gerenciar o sensor de som
typedef struct {
    uint16_t baseline;
    uint16_t samples[SOUND_SAMPLES];
    uint8_t sample_index;
    uint32_t last_report;
} SoundSensor;

static SoundSensor sound = {
    .baseline = 2000,
    .sample_index = 0,
    .last_report = 0
};

// Protótipos das funções
static void calibrate_sound_sensor(void);
static void process_sound(void);
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Função de calibração do sensor
static void calibrate_sound_sensor(void) {
    printf("Calibrando sensor de som...\n");
    
    uint32_t sum = 0;
    for(int i = 0; i < SOUND_SAMPLES; i++) {
        adc_select_input(SOUND_ADC_INPUT);
        sound.samples[i] = adc_read();
        sum += sound.samples[i];
        sleep_ms(10);
    }
    
    sound.baseline = sum / SOUND_SAMPLES;
    printf("Calibração concluída. Baseline: %d\n", sound.baseline);
}

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Controle dos LEDs
    if (strstr(request, "GET /blue_on") != NULL)
    {
        gpio_put(LED_BLUE_PIN, 1);
    }
    else if (strstr(request, "GET /blue_off") != NULL)
    {
        gpio_put(LED_BLUE_PIN, 0);
    }
    else if (strstr(request, "GET /green_on") != NULL)
    {
        gpio_put(LED_GREEN_PIN, 1);
    }
    else if (strstr(request, "GET /green_off") != NULL)
    {
        gpio_put(LED_GREEN_PIN, 0);
    }
    else if (strstr(request, "GET /red_on") != NULL)
    {
        gpio_put(LED_RED_PIN, 1);
    }
    else if (strstr(request, "GET /red_off") != NULL)
    {
        gpio_put(LED_RED_PIN, 0);
    }
    else if (strstr(request, "GET /on") != NULL)
    {
        cyw43_arch_gpio_put(LED_PIN, 1);
    }
    else if (strstr(request, "GET /off") != NULL)
    {
        cyw43_arch_gpio_put(LED_PIN, 0);
    }

    // Leitura da temperatura interna
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    float temperature = 27.0f - ((raw_value * conversion_factor) - 0.706f) / 0.001721f;

    // Cria a resposta HTML
    char html[1024];

    snprintf(html, sizeof(html),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Refresh: 1\r\n"  // Atualiza a página a cada 1 segundo
             "\r\n"
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title>Monitoramento BitDog</title>\n"
             "<style>\n"
             "body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }\n"
             "h1 { font-size: 48px; margin-bottom: 30px; }\n"
             ".status-box { border: 2px solid #ccc; margin: 20px auto; padding: 20px; max-width: 600px; }\n"
             ".status-item { font-size: 24px; margin: 10px; padding: 10px; }\n"
             ".high { color: red; }\n"
             ".medium { color: orange; }\n"
             ".low { color: green; }\n"
             "</style>\n"
             "</head>\n"
             "<body>\n"
             "<h1>Monitor BitDog</h1>\n"
             "<div class='status-box'>\n"
             "<div class='status-item'>\n"
             "Botão A: <span style='color: %s'>%s</span>\n"
             "</div>\n"
             "<div class='status-item'>\n"
             "Botão B: <span style='color: %s'>%s</span>\n"
             "</div>\n"
             "<div class='status-item'>\n"
             "Som Detectado: <span class='%s'>%s</span>\n"
             "(ADC: %d, Variação: %d)\n"
             "</div>\n"
             "<div class='status-item'>\n"
             "Temperatura: %.1f °C\n"
             "</div>\n"
             "</div>\n"
             "<div style='margin-top: 30px;'>\n"
             "<p><small>Última atualização: %02d:%02d:%02d</small></p>\n"
             "</div>\n"
             "</body>\n"
             "</html>\n",
             last_button_a_state ? "green" : "red",
             last_button_a_state ? "Solto" : "Pressionado",
             last_button_state ? "green" : "red",
             last_button_state ? "Solto" : "Pressionado",
             strcmp(last_sound_level, "ALTO") == 0 ? "high" : 
             strcmp(last_sound_level, "MEDIO") == 0 ? "medium" : "low",
             last_sound_level,
             last_sound_value,
             last_sound_variation,
             temperature,
             global_time_ms / (60 * 60 * 1000),      // horas
             (global_time_ms / (60 * 1000)) % 60,    // minutos
             (global_time_ms / 1000) % 60            // segundos
    );

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    free(request);
    pbuf_free(p);

    return ERR_OK;
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Função de verificação dos botões atualizada
static void check_button_status(void) {
    bool current_state_b = gpio_get(BUTTON_B_PIN);
    if (current_state_b != last_button_state) {
        printf("Botão B: %s\n", current_state_b ? "Solto" : "Pressionado");
        last_button_state = current_state_b;
    }

    bool current_state_a = gpio_get(BUTTON_A_PIN);
    if (current_state_a != last_button_a_state) {
        printf("Botão A: %s\n", current_state_a ? "Solto" : "Pressionado");
        last_button_a_state = current_state_a;
    }
}

// Função para média do ADC
static uint16_t read_adc_averaged(void) {
    uint32_t sum = 0;
    for(int i = 0; i < ADC_SAMPLES; i++) {
        sum += adc_read();
    }
    return sum / ADC_SAMPLES;
}

// Função de conversão ADC para frequência
static float adc_to_frequency(uint16_t adc_value) {
    if (adc_value < NOISE_THRESHOLD) {
        return 0.0f;
    }
    float normalized = (float)(adc_value - NOISE_THRESHOLD) / (float)((1 << 12) - NOISE_THRESHOLD);
    return FREQ_MIN + (normalized * (FREQ_MAX - FREQ_MIN));
}

// Função para leitura do sensor de som
static float read_sound_frequency(void) {
    adc_select_input(SOUND_ADC_INPUT);
    uint16_t raw_value = read_adc_averaged();
    return adc_to_frequency(raw_value);
}

// Função para ler o nível de som
static uint16_t read_sound_level(void) {
    uint32_t sum = 0;
    for(int i = 0; i < ADC_SAMPLES; i++) {
        sum += adc_read();
        sleep_us(100);
    }
    return sum / ADC_SAMPLES;
}

// Nova função para leitura do sensor com debug
static void read_sound_with_debug(void) {
    adc_select_input(SOUND_ADC_INPUT);
    
    uint16_t current_value = adc_read();
    
    // Atualiza valores mínimos e máximos
    if (current_value < min_adc) min_adc = current_value;
    if (current_value > max_adc) max_adc = current_value;
    
    sample_count++;
    
    // Log detalhado a cada segundo
    if (sample_count % 10 == 0) {
        printf("Som - ADC: %d, Min: %d, Max: %d, Variação: %d\n", 
               current_value, 
               min_adc, 
               max_adc,
               max_adc - min_adc);
    }
}

// Ajustar as definições do ADC
#define LOG_INTERVAL_MS 50      // Reduzir intervalo para mais sensibilidade
#define WINDOW_SIZE 10          // Janela de amostras para detecção de pico

// Nova função para detecção de picos
static void detect_sound_peaks(void) {
    static uint16_t samples[WINDOW_SIZE];
    static int sample_index = 0;
    static uint16_t baseline = 0;
    
    // Lê valor atual
    adc_select_input(SOUND_ADC_INPUT);
    uint16_t current = adc_read();
    
    // Calcula linha base (média móvel)
    samples[sample_index] = current;
    sample_index = (sample_index + 1) % WINDOW_SIZE;
    
    uint32_t sum = 0;
    for(int i = 0; i < WINDOW_SIZE; i++) {
        sum += samples[i];
    }
    baseline = sum / WINDOW_SIZE;
    
    // Calcula variação em relação à linha base
    int16_t variation = current - baseline;
    
    // Só mostra se houver variação significativa
    if (abs(variation) > 10) {  // Threshold de detecção
        printf("Som detectado! ADC: %d, Variação: %d, Baseline: %d\n", 
               current, variation, baseline);
    }
}

// Ajuste das constantes para melhor sensibilidade
#define SOUND_THRESHOLD_LOW    50   // Para sons baixos
#define SOUND_THRESHOLD_MED   100   // Para sons médios
#define SOUND_THRESHOLD_HIGH  200   // Para sons altos

// Função melhorada para classificar os sons
static void process_sound(void) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    if (current_time - sound.last_report < MIN_REPORT_TIME) {
        return;
    }
    
    adc_select_input(SOUND_ADC_INPUT);
    uint16_t current = adc_read();
    
    // Atualiza média móvel
    sound.samples[sound.sample_index] = current;
    sound.sample_index = (sound.sample_index + 1) % SOUND_SAMPLES;
    
    uint32_t sum = 0;
    for(int i = 0; i < SOUND_SAMPLES; i++) {
        sum += sound.samples[i];
    }
    uint16_t avg = sum / SOUND_SAMPLES;
    
    // Calcula variação
    int16_t variation = current - sound.baseline;
    int var_abs = abs(variation);
    
    // Atualiza variáveis globais
    last_sound_value = current;
    last_sound_variation = variation;
    
    // Classifica o som por intensidade
    const char* nivel;
    if (var_abs > SOUND_THRESHOLD_HIGH) {
        nivel = "ALTO";
    } else if (var_abs > SOUND_THRESHOLD_MED) {
        nivel = "MEDIO";
    } else if (var_abs > SOUND_THRESHOLD_LOW) {
        nivel = "BAIXO";
    } else {
        return; // Ignora variações muito pequenas
    }
    
    strncpy(last_sound_level, nivel, sizeof(last_sound_level));
    
    printf("Som: ADC=%d, Var=%d, Med=%d, Nivel=%s\n", 
           current, variation, avg, last_sound_level);
    sound.last_report = current_time;
}

// Função principal
int main()
{
    stdio_init_all();
    sleep_ms(2000);  // Aguarda inicialização da porta serial

    printf("\n---- A porta serial COM2 abriu ----\n");
    
    // Inicialização do WiFi
    if (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi\n");
        return -1;
    }

    cyw43_arch_enable_sta_mode();
    
    printf("Conectando ao Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        return -1;
    }

    printf("Conectado ao Wi-Fi\n");
    
    if (netif_default) {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Adicionar esta parte - Inicialização do servidor TCP
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Falha ao criar PCB TCP\n");
        return -1;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, 80);
    if (err != ERR_OK) {
        printf("Falha ao fazer bind na porta 80\n");
        return -1;
    }

    pcb = tcp_listen(pcb);
    if (!pcb) {
        printf("Falha ao iniciar modo listen\n");
        return -1;
    }

    tcp_accept(pcb, tcp_server_accept);
    
    printf("Servidor ouvindo na porta 80\n");
    
    // Inicialização dos pinos e ADC
    gpio_init(BUTTON_B_PIN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_B_PIN);

    gpio_init(BUTTON_A_PIN);
    gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_A_PIN);

    adc_init();
    adc_gpio_init(SOUND_SENSOR_PIN);
    calibrate_sound_sensor();

    // Calibração inicial do sensor de som
    uint32_t sum = 0;
    for(int i = 0; i < 16; i++) {
        sum += adc_read();
        sleep_ms(10);
    }
    baseline_adc = sum / 16;

    printf("Sistema pronto!\n");
    printf("--------------------\n\n");
    
    uint32_t last_poll_time = 0;
    
    while (true) {
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        global_time_ms = current_time;  // Atualiza o tempo global
        
        if (current_time - last_poll_time >= POLL_INTERVAL_MS) {
            check_button_status();
            process_sound();
            last_poll_time = current_time;
        }
        
        // Garante tempo para o servidor web
        cyw43_arch_poll();
        sleep_ms(10);
    }

    return 0;
}