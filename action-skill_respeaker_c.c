#include "action-skill_respeaker_c.h"
// all share
APA102 leds = {0, -1, NULL};
short last_state = 0;
short curr_state = 0;

// self use
// devices number
int         fd_sock = -1;
pthread_t   curr_thread;
const char	*addr;
const char	*port;
char 		*client_id;

const char* topic[NUM_TOPIC]={
	"hermes/hotword/toggleOff",
    "hermes/asr/startListening",
    "hermes/asr/stopListening",
    "hermes/tts/say",
    "hermes/tts/sayFinished",
    "hermes/hotword/toggleOn",
    "hermes/feedback/sound/toggleOn",
    "hermes/feedback/sound/toggleOff",
    "hermes/nlu/intentNotRecognized",
    "hermes/nlu/intentParsed",
};

snipsSkillConfig configList[]={
    {"model", "rsp2mic"},
    {"spi_dev", "0:0"},
    {"led_num", "3"},
    {"mqtt_host", "localhost"},
    {"mqtt_port", "1883"},
    {"if_idle", "True"},
    {"if_listen", "True"},
    {"if_think", "True"},
    {"if_mute", "True"},
    {"if_unmute", "True"}
};

void (*status[9])(const char *)={ 
	on_idle, 
	on_listen, 
	on_think, 
	on_speak, 
	to_mute, 
	to_unmute, 
	on_success, 
	on_error, 
	on_off
};

int main(int argc, char const *argv[])
{
	// generate a random id as client id
	client_id = generate_client_id();

	// get input parameters
    leds.numLEDs = (argc > 1)? atoi(argv[1]) : 3;
    addr = (argc > 2)? argv[2] : "localhost";
    port = (argc > 3)? argv[3] : "1883";
    
    pthread_t client_daemon;
    client_daemon = mqtt_setup();
	apa102_spi_setup();

	printf("Press CTRL-D to exit.\n\n");
	printf("Press CTRL-D to exit.\n\n");
    
    // start block
    while(fgetc(stdin) != EOF);

    // disconnect
    printf("\n%s disconnecting from %s\n", argv[0], addr);
    sleep(1);

    // clean
    close_all(EXIT_SUCCESS, &client_daemon);
	return 0;
}

void apa102_spi_setup(){
    int temp;
    leds.pixels = (uint8_t *)malloc(leds.numLEDs * 4);
    begin();
    if((temp = pthread_create(&curr_thread, NULL, on_idle, NULL)) != 0){
        printf("[Error] Failed to create 1st thread!\n"); 
    	close_all(EXIT_FAILURE, NULL);
    }
}

pthread_t mqtt_setup(){
	int i;
	fd_sock = open_nb_socket(addr, port);
	if (fd_sock == -1) {
        perror("[Error] Failed to open socket!\n");
        close_all(EXIT_FAILURE, NULL);
    }

    struct mqtt_client client;
    uint8_t sendbuf[2048]; /* sendbuf should be large enough to hold multiple whole mqtt messages */
    uint8_t recvbuf[1024]; /* recvbuf should be large enough any whole mqtt message expected to be received */
    mqtt_init(&client, fd_sock, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);
    mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, 0, 400);

    // check that we don't have any errors
    if (client.error != MQTT_OK) {
        fprintf(stderr, "[Error] %s\n", mqtt_error_str(client.error));
        close_all(EXIT_FAILURE, NULL);
    }

    // start a thread to refresh the client (handle egress and ingree client traffic)
    pthread_t client_daemon;
    if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "[Error] Failed to start client daemon.\n");
        close_all(EXIT_FAILURE, NULL);

    }

    // subscribe to topics
    for(i=0;i<NUM_TOPIC;i++){
        mqtt_subscribe(&client, topic[i], 0);
        printf("[Info] Subscribed to '%s'.\n", topic[i]);
    }

    return client_daemon;
}

void publish_callback(void** unused, struct mqtt_response_publish *published) {
    /* note that published->topic_name is NOT null-terminated (here we'll change it to a c-string) */
    char* topic_name = (char*) malloc(published->topic_name_size + 1);
    memcpy(topic_name, published->topic_name, published->topic_name_size);
    topic_name[published->topic_name_size] = '\0';

    printf("[Received] %s \n", topic_name);

    switch(curr_state){
        case 0:
            if (strcmp(topic_name, "hermes/hotword/toggleOff") == 0)
                last_state = curr_state, curr_state = 1;
            else if (strcmp(topic_name, "hermes/feedback/sound/toggleOff") == 0)
                last_state = curr_state, curr_state = 4;
            else if (strcmp(topic_name, "hermes/feedback/sound/toggleOn") == 0)
                last_state = curr_state, curr_state = 5;
            break;
        case 1:
            if (strcmp(topic_name, "hermes/asr/stopListening") == 0)
                last_state = curr_state, curr_state = 2;
            else if (strcmp(topic_name, "hermes/hotword/toggleOn") == 0)
                last_state = curr_state, curr_state = 0;
            break;
        case 2:
            if (strcmp(topic_name, "hermes/tts/say") == 0)
                last_state = curr_state, curr_state = 3;
            else if (strcmp(topic_name, "hermes/hotword/toggleOn") == 0)
                last_state = curr_state, curr_state = 0;
            break;
        case 3:
            if (strcmp(topic_name, "hermes/tts/sayFinished") == 0)
                last_state = curr_state, curr_state = 0;
            else if (strcmp(topic_name, "hermes/hotword/toggleOn") == 0)
                last_state = curr_state, curr_state = 0;
            break;
        
    }

    printf("[Info] State is changed to %d\n", curr_state);
    if (last_state != curr_state)
        pthread_create(&curr_thread, NULL, status[curr_state], NULL);

    free(topic_name);
}

void* client_refresher(void* client){
    while(1) 
    {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}

char* generate_client_id(){
    int i ,flag;
    static char id[CLIENT_ID_LEN + 1] = {0};
    srand(time(NULL));
    for (i = 0; i < CLIENT_ID_LEN; i++){
        flag = rand()%3;
        switch(flag){
            case 0:
                id[i] = rand()%26 + 'a';
                break;
            case 1:
                id[i] = rand()%26 + 'A';
                break;
            case 2:
                id[i] = rand()%10 + '0';
                break;
        }
    }
    return id;
}

void close_all(int status, pthread_t *client_daemon){
    show();
    if (fd_sock != -1) close(fd_sock);
    if (leds.fd_spi != -1) close(leds.fd_spi);
    if (leds.pixels) free(leds.pixels);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}

