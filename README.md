# 🌱 AgroVista — Estação IoT de Talhão v2.0

> Inteligência climática hiperlocal para o produtor rural brasileiro.

Protótipo IoT para monitoramento climático de talhão agrícola com **simulação de crises**, **MQTT** e **dashboard Node-RED**. O sistema coleta dados de sensores locais, classifica o status do talhão em tempo real e envia alertas acionáveis via MQTT — conectando o campo ao dashboard do produtor.

---

## 👥 Equipe

| Nome |
|---|
| Iago Liziero Pereira |
| Ícaro José dos Santos |
| Leonardo Barbosa Santos |
| Gustavo Souto de Melo |

---

## 📋 Informações do Projeto

| Campo | Detalhe |
|---|---|
| **Disciplina** | Disruptive Architectures: IoT, IoB & Generative AI |
| **Entrega** | Global Solution — FIAP |
| **Simulação** | Wokwi (ESP32) |
| **Broker MQTT** | broker.hivemq.com |
| **Dashboard** | Node-RED |
| **Persona de teste** | João Batista · Produtor · 590 ha · Sorriso/MT |

---

## 💡 Sobre o AgroVista

O AgroVista transforma dados de sensores locais em decisões simples para o produtor rural. Em vez de receber índices técnicos, o produtor vê em tempo real o status do talhão e recebe alertas diretos como **"Solo crítico — irrigação imediata"** ou **"Risco de geada — proteger culturas"**.

Uma safra de soja no Mato Grosso vale em média R$ 10.000/ha. Evitar 5% de perda em 500 hectares preserva R$ 250.000 — o AgroVista torna essa proteção acessível e automatizada.

---

## 🧩 Componentes do Hardware

| Componente | Pino | Função | Tipo |
|---|---|---|---|
| DHT22 | 4 | Temperatura + Umidade do ar | **Entrada** |
| LDR (fotoresistor) | 34 | Simula umidade do solo | **Entrada** |
| Botão SECA | 18 | Simula crise de seca extrema | **Entrada** |
| Botão GEADA | 19 | Simula risco de geada | **Entrada** |
| LED RGB | R=25 G=26 B=27 | Indicador visual de status | **Saída** |
| Buzzer | 32 | Alarme sonoro em alerta | **Saída** |
| LCD 16x2 I2C | SDA=21 SCL=22 | Interface local de dados | **Interface** |

### LED RGB — Código de cores

| Cor | Significado |
|---|---|
| 🟢 Verde | Condições normais |
| 🟡 Amarelo | Atenção — solo abaixo do ideal |
| 🔴 Vermelho | Crítico — ação imediata necessária |
| 🔵 Azul | Risco de geada |

---

## 🌐 Tópicos MQTT

**Broker:** `broker.hivemq.com:1883` (público, sem autenticação)  
**Client ID:** gerado aleatoriamente a cada boot via `esp_random()` para evitar colisão de sessões.

### `agrovista/talhao/temperatura`
Publicado a cada 4 segundos.
```json
{ "valor": 32.5, "umidade_ar": 45.0 }
```

### `agrovista/talhao/solo`
Publicado a cada 4 segundos.
```json
{ "umidade_solo": 38, "luminosidade": 512 }
```

### `agrovista/talhao/status`
Status consolidado do talhão.
```json
{ "status": "normal", "temp": 32.5, "solo": 72, "ar": 45.0 }
```
> Valores possíveis de `status`: `"normal"` | `"atencao"` | `"critico"`

### `agrovista/talhao/alerta`
Publicado **apenas quando status = crítico**.
```json
{ "alerta": true, "motivo": "seca_critica", "talhao": "Sul", "produtor": "Joao Batista" }
```
> Motivos possíveis: `seca_critica` | `calor_extremo` | `risco_geada`

### `agrovista/talhao/simulacao`
Publicado quando botão de simulação está pressionado.
```json
{ "modo": "SECA", "ativo": true }
```

---

## 🚨 Lógica de Alertas

| Condição | Limiar | Status | LED | Buzzer |
|---|---|---|---|---|
| Umidade solo | < 25% | Crítico | 🔴 | 3 bips |
| Temperatura | > 40°C | Crítico | 🔴 | 3 bips |
| Temperatura | < 8°C | Crítico | 🔵 | 3 bips |
| Umidade solo | 25–50% | Atenção | 🟡 | 1 bip |
| Solo OK + Temp OK | — | Normal | 🟢 | — |

---

## 🎮 Simulação de Situações Críticas

| Botão | Simula | Valores injetados |
|---|---|---|
| **SECA** (vermelho) | Seca extrema no talhão | Solo: 10% · Temp: 41.5°C |
| **GEADA** (azul) | Risco de geada noturna | Temp: 4.0°C |

Durante a simulação, o ESP32 publica no tópico `agrovista/talhao/simulacao` indicando o modo ativo. Os valores injetados substituem a leitura real dos sensores, permitindo demonstrar o comportamento completo do sistema sem precisar de condições climáticas reais.

---

## 🖥️ LCD — Páginas alternadas (a cada 3s)

```
Página 1:               Página 2:
T:32.5C Ar:45%         SIM: SECA CRITICA
Solo:38% ALERTA!       ! Verificar campo
```

---

## 📊 Node-RED — Dashboard

### Como importar o flow

1. Abra o Node-RED (`http://localhost:1880`)
2. Menu → **Import** → cole o conteúdo de `node-red-flow.json`
3. Instale os nodes necessários via **Manage Palette**:
   - `node-red-dashboard`
4. Clique em **Deploy**
5. Acesse o dashboard em `http://localhost:1880/ui`

### O que o dashboard exibe

| Widget | Dados |
|---|---|
| Gauge Temperatura | °C em tempo real (0–50°C) |
| Gauge Umidade Ar | % em tempo real |
| Gauge Umidade Solo | % com zonas coloridas |
| Status do Talhão | Normal / Atenção / Crítico |
| Recomendação | Texto acionável para o produtor |
| Último Alerta | Texto completo do alerta recebido |
| Toast de notificação | Dispara automaticamente em alertas críticos |
| Gráfico Temperatura | Linha histórica |
| Gráfico Solo | Linha histórica |
| Modo Simulação | Indica se botão de crise está ativo |
| Inject de teste | Dispara alerta manual sem hardware |

---

## 🚀 Como simular no Wokwi

1. Acesse [wokwi.com](https://wokwi.com) → **New Project → ESP32**
2. Cole o `wifi-scan.ino` no editor
3. Substitua o `diagram.json`
4. Instale as bibliotecas listadas em `libraries.txt`
5. Clique em **Start Simulation**

### Demonstrar alerta de seca
Pressione o botão **SECA** (vermelho) → LED fica vermelho, buzzer dispara 3 bips, MQTT publica alerta, dashboard atualiza.

### Demonstrar risco de geada
Pressione o botão **GEADA** (azul) → LED fica azul, buzzer dispara 3 bips, LCD mostra "SIM: RISCO GEADA".

### Simular solo seco manualmente
Ajuste o LDR (fotoresistor) para luminosidade alta → umidade do solo cai → sistema entra em atenção ou crítico.

---

## 📁 Estrutura do Repositório

```
agrovista-iot-v2/
├── wifi-scan.ino          ← Código principal ESP32
├── diagram.json        ← Diagrama de componentes Wokwi
├── libraries.txt       ← Bibliotecas necessárias
├── node-red-flow.json  ← Flow completo para importar no Node-RED
└── README.md           ← Esta documentação
```

---

## 🛠️ Stack Técnica

| Camada | Tecnologia |
|---|---|
| Microcontrolador | ESP32 DevKit C v4 |
| Linguagem | C++ (Arduino framework) |
| Protocolo IoT | MQTT via PubSubClient |
| Broker | broker.hivemq.com (público) |
| Dashboard | Node-RED + node-red-dashboard |
| Simulação | Wokwi |
| Serialização | ArduinoJson |