#include <Arduino.h>
#include <LoraMesher.h>



#include <BluetoothSerial.h>
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

//Libraries for OLED Display
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//OLED pins
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

BluetoothSerial SerialBT;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

LoraMesher& radio = LoraMesher::getInstance();

TaskHandle_t receiveLoRaMessage_Handle = NULL;

#define datalength 190  
#define reqack "ack"
#define reqstart "istartchat"

enum messagetype {
    searchreq,
    searchack,
    chatreq,
    chatack,
    chatmessage
};

struct dataPacket {
    messagetype type;
    char data[datalength];

};

char myname[datalength] = "test";



struct contactinfo {
    uint16_t addres = 0;
    String name;
};

enum chatstate {
    setuptime,
    handshake,
    chat
};

contactinfo contacts[256];

chatstate mode = setuptime;
uint16_t chataddres;
bool istartchat = false;
bool hasbtclient = false;

//Led flash
void led_Flash(uint16_t flashes, uint16_t delaymS) {
    uint16_t index;
    for (index = 1; index <= flashes; index++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(delaymS);
        digitalWrite(LED_BUILTIN, LOW);
        delay(delaymS);
    }
}

void searchcontacts() {
    int i = 0;
    while (i < 256) {
        if (contacts[i].addres == 0)break;
        contacts[i].addres = 0;
    }
    dataPacket* helloPacket = new dataPacket;

    helloPacket->type = searchreq;
    radio.createPacketAndSend(BROADCAST_ADDR, helloPacket, 1);
}



void tractarPacket(AppPacket<dataPacket>* packet) {
    dataPacket* dp = packet->payload;

    Serial.println(dp->data);
    Serial.println(dp->type);

    if (dp->type == searchreq) {
        dataPacket* ack = new dataPacket();

        ack->type = searchack;
        strcpy(ack->data, myname);
        radio.createPacketAndSend(packet->src, ack, 1);

    }
    else if (dp->type == searchack) {

        bool searching = true;
        int i = 0;
        while (searching && i < 256) {
            if (contacts[i].addres == 0 || contacts[i].addres == packet->src) {
                searching = false;
                break;
            }
            ++i;
        }
        if (contacts[i].addres == 0) {
            contacts[i].addres = packet->src;
            contacts[i].name = dp->data;
            Serial.println("new contact added:");
            Serial.println(contacts[i].addres);
            Serial.println(contacts[i].name);
            Serial.println(i);
        }
        if (searching) Serial.println("error: no space in local contacts");
    }
    else if (dp->type == chatreq) {
        if (mode == setuptime) {
            Serial.println(strcmp(dp->data, reqstart));
            if (!strcmp(dp->data, reqstart)) {
                istartchat = false;
                Serial.println("mode: someone tring to start chat with me");
                mode = handshake;
                chataddres = packet->src;
                SerialBT.println("accept(yes/exit) chat request from? " + (String) chataddres);
            }
            else {//data==ack
                dataPacket* fichat = new dataPacket();
                fichat->type = chatack;
                radio.createPacketAndSend(packet->src, fichat, 1);
            }

        }
        else if (mode == handshake && packet->src == chataddres) {
            if (!strcmp(dp->data, reqstart) && !istartchat) {
                dataPacket* ack = new dataPacket();
                strcpy(ack->data, reqack);
                ack->type = chatreq;
                radio.createPacketAndSend(packet->src, ack, 1);
            }
            else if (!strcmp(dp->data, reqack) && istartchat) {
                mode = chat;
                SerialBT.println("starting chat with(exit to end chat): " + (String) chataddres);
            }
            else {
                mode = setuptime;
                dataPacket* fichat = new dataPacket();
                fichat->type = chatack;
                radio.createPacketAndSend(packet->src, fichat, 1);
            }
        }
        else if (packet->src != chataddres) {
            Serial.println("mode: someone tring to start chat with me but i am busy with someone else");
            dataPacket* chatackpacket = new dataPacket();
            chatackpacket->type = chatack;
            radio.createPacketAndSend(packet->src, chatackpacket, 1);
        }

    }
    else if (dp->type == chatack) {
        if ((mode == handshake || mode == chat) && packet->src == chataddres) {
            mode = setuptime;
            SerialBT.println((String) chataddres + " is canceling the chat");
        }
    }
    else if (dp->type == chatmessage && mode == chat && packet->src == chataddres) {
        SerialBT.println((String) chataddres + ": " + dp->data);

    }

}


/**
 * @brief Function that process the received packets
 *
 */
void processReceivedPackets(void*) {
    for (;;) {
        /* Wait for the notification of processReceivedPackets and enter blocking */
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);


        led_Flash(1, 100); //one quick LED flashes to indicate a packet has arrived

        //Iterate through all the packets inside the Received User Packets FiFo
        while (radio.getReceivedQueueSize() > 0) {
            Log.trace(F("ReceivedUserData_TaskHandle notify received" CR));
            Log.trace(F("Fifo receiveUserData size: %d" CR), radio.getReceivedQueueSize());

            //Get the first element inside the Received User Packets FiFo
            AppPacket<dataPacket>* userp = radio.getNextAppPacket<dataPacket>();

            //Print the data packet
            char buf[128];
            sprintf(buf, "----paquet rebut de: %i a: %i size: %i", userp->src, userp->dst, userp->payloadSize);
            Serial.println(buf);
            tractarPacket(userp);

            //Delete the packet when used. It is very important to delete the packets.
            radio.deletePacket(userp);
        }
    }
}

/**
 * @brief Create a Receive Messages Task and add it to the LoRaMesher
 *
 */
void createReceiveMessages() {
    int res = xTaskCreate(
        processReceivedPackets,
        "Receive App Task",
        4096,
        (void*) 1,
        2,
        &receiveLoRaMessage_Handle);
    if (res != pdPASS) {
        Log.errorln(F("Receive App Task creation gave error: %d"), res);
    }

    radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
}

/**
 * @brief Initialize LoRaMesher
 *
 */
void setupLoraMesher() {
    //Init the loramesher with a processReceivedPackets function
    radio.begin();

    //Create the receive task and add it to the LoRaMesher
    createReceiveMessages();

    //Start LoRaMesher
    radio.start();

    Log.verboseln("LoRaMesher initialized");
}

void printhelp() {
    SerialBT.println("Available commands:\n  help\n  searchcontacts\n  printcontacts\n  sendcontacts\n  namechange <name>\n  chat <address>");
}

void printcontacts() {
    Serial.println("contacts:");
    int i = 0;
    while (i < 256 && contacts[i].addres != 0) {
        Serial.print(i);
        Serial.print("  addr: ");Serial.print(contacts[i].addres);
        Serial.print("  name: ");Serial.println(contacts[i].name);

        ++i;
    }
}

void sendcontacts() {

    SerialBT.println("contacts:");

    if (contacts[0].addres == 0) {
        SerialBT.println("its empty");
    }
    else {
        int i = 0;
        while (i < 256 && contacts[i].addres != 0) {
            SerialBT.println(" addr: " + ((String) contacts[i].addres) + " name: " + contacts[i].name);
            ++i;
        }
    }
}

void namechange(String name) {
    name.toCharArray(myname, datalength);
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    if (event == ESP_SPP_SRV_OPEN_EVT and !hasbtclient) {
        hasbtclient = true;

        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("BT client connected");
        display.setCursor(0, 10);
        display.print("LoRa BT addrs: " + (String) radio.getLocalAddress());
        display.setCursor(0, 20);
        display.print("Last message from BT:");
        display.setCursor(0, 30);
        display.print("<no message reseived yet>");
        //display.display();
    }
    else if (event == ESP_SPP_CLOSE_EVT && !SerialBT.hasClient()) {
        hasbtclient = false;
        if (mode != setuptime) {
            mode = setuptime;
            dataPacket* fichat = new dataPacket();
            fichat->type = chatack;
            radio.createPacketAndSend(chataddres, fichat, 1);
        }
        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("BTclient disconnected");
        display.setCursor(0, 10);
        display.print("LoRa BT addrs: " + (String) radio.getLocalAddress());
        display.setCursor(0, 20);
        display.print("waiting for a BT client to connect");
        //display.display();
    }
}

void setup() {
    //initialize Serial Monitor
    Serial.begin(115200);

    //reset OLED display via software
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(20);
    digitalWrite(OLED_RST, HIGH);

    //initialize OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
        Serial.println(F("SSD1306 allocation failed"));
        for (;;); // Don't proceed, loop forever
    }

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("mesherselect project");
    display.display();

    Serial.println("mesherselect project");


    setupLoraMesher();

    Serial.println("LoRa Initializing OK!");
    display.setCursor(0, 10);
    display.print("LoRa Initializing OK!");
    display.display();

    SerialBT.register_callback(callback);
    if (!SerialBT.begin((String) radio.getLocalAddress())) {
        Serial.println("BT init error");
        display.setCursor(0, 20);
        display.print("BT init error");
        display.display();
        for (;;);
    }
    Serial.println("BT Initializing OK!");
    display.setCursor(0, 20);
    display.print("BT Initializing OK!");
    display.setCursor(0, 30);
    display.print("LoRa BT addrs: " + (String) radio.getLocalAddress());
    display.setCursor(0, 40);
    display.print("waiting for a BT client to connect");
    display.display();
    delay(2000);

}



void loop() {
    while (SerialBT.available()) {
        String message = SerialBT.readStringUntil('\n');
        Serial.println("BT: " + message);

        display.fillRect(0, 30, SCREEN_WIDTH, 40, BLACK);//clear line
        display.setCursor(0, 30);
        display.print(message);

        if (mode == setuptime) {
            if (message == "searchcontacts")searchcontacts();
            else if (message == "help")printhelp();
            else if (message == "printcontacts")printcontacts();
            else if (message == "sendcontacts")sendcontacts();
            else if (message == "namechange") {
                while (!SerialBT.available())delay(20);
                String name = SerialBT.readString();
                if (name.length() <= datalength) {
                    namechange(name);
                    SerialBT.println("Name changed to: " + name);

                }
                else {
                    SerialBT.println("Name is too long, has to be smaller (" + (String) datalength + " char max");
                }

            }
            else if (message == "chat") {
                while (!SerialBT.available())delay(20);
                String addr = SerialBT.readStringUntil('\n');
                Serial.println("BT: " + addr);
                if ((int) floor(log(addr.toInt()) / log(10)) + 1 != addr.length()) {
                    SerialBT.println("this: " + addr + " is not an address");
                }
                else {
                    int i = 0;
                    while (i < 256) {
                        if (contacts[i].addres == addr.toInt()) break;
                        ++i;
                    }

                    if (i == 256)SerialBT.println("this address: " + addr + " is not in contacts");
                    else {
                        if (RoutingTableService::hasAddressRoutingTable(contacts[i].addres)) {
                            dataPacket* chatreqpacket = new dataPacket();
                            strcpy(chatreqpacket->data, reqstart);
                            chatreqpacket->type = chatreq;
                            radio.createPacketAndSend(contacts[i].addres, chatreqpacket, 1);
                            SerialBT.println("tring to start chat with address: " + addr + " name: " + contacts[i].name);
                            mode = handshake;
                            istartchat = true;
                            chataddres = contacts[i].addres;

                        }
                        else {
                            SerialBT.println("target is now unreachable: " + addr);
                        }
                    }
                }


            }
            else SerialBT.println(message + " is invalid command");

        }
        else if (mode == handshake) {
            if (message == "exit") {
                SerialBT.println("canceling chat with: " + (String) chataddres);
                mode = setuptime;
            }
            else if (message == "yes") {
                dataPacket* chatreqpacket = new dataPacket();
                chatreqpacket->type = chatreq;
                if (istartchat) {
                    strcpy(chatreqpacket->data, reqstart);

                }
                else {
                    strcpy(chatreqpacket->data, reqack);
                    mode = chat;
                    SerialBT.println("starting chat with(exit to end chat): " + (String) chataddres);

                }
                radio.createPacketAndSend(chataddres, chatreqpacket, 1);
            }
            else {
                SerialBT.println("resend request to " + (String) chataddres + " ? (yes/exit)");
            }
        }
        else {//mode==chat
            if (message == "exit") {
                SerialBT.println("canceling chat with: " + (String) chataddres);
                mode = setuptime;
                dataPacket* fichat = new dataPacket();

                fichat->type = chatack;
                radio.createPacketAndSend(chataddres, fichat, 1);
            }
            else {
                int i = 0;

                while (message.length() >= datalength * i) {

                    dataPacket* chatmes = new dataPacket();
                    char buf[datalength];
                    message.substring(i * (datalength - 1), (i + 1) * (datalength - 1)).toCharArray(buf, datalength);
                    strcpy(chatmes->data, buf);
                    chatmes->type = chatmessage;
                    radio.createPacketAndSend(chataddres, chatmes, 1);
                    ++i;
                }
            }
        }
    }
    delay(50);
    display.display();
}