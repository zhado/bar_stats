#include <bits/types.h>
#include <pulse/volume.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <pulse/pulseaudio.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

int SLEEP_TIME=1000000;
typedef struct network_thread_data{
	char network_output[100];
	FILE* rx_bytes_f;
	FILE* tx_bytes_f;
	FILE* rx_packets_f;
	FILE* tx_packets_f;
}network_thread_data;

typedef struct cpu_usage_thread_data{
	int stat_fd;
	float ratio;
}cpu_usage_thread_data;


typedef struct pulse_data{
	pa_threaded_mainloop* main_loop;
	pa_mainloop_api* mainloop_api;
	pa_context* context;
	int pulse_ready;
	int vol;
}pulse_data;

void reset_fp(FILE* ptr){
	fseek( ptr, 0, SEEK_END );
	rewind(ptr);
}

void info_cb (pa_context *c, const pa_sink_info *i, int eol, void *userdata){
	if(eol)
		return;
	float vol=((float)i->volume.values[0])/((float)PA_VOLUME_NORM)*100;
	((pulse_data*)userdata)->vol=vol;
}

void context_change_cb(pa_context *c, void* userdata){
	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_READY:{
			((pulse_data*)userdata)->pulse_ready=1;
		}break;
		default:
		break;
	}
}

void pulse_init(pulse_data* p_data){
	p_data->pulse_ready=0;
	p_data->vol=-1;
	p_data->main_loop=pa_threaded_mainloop_new();
	pa_threaded_mainloop_lock(p_data->main_loop);
	p_data->mainloop_api= pa_threaded_mainloop_get_api(p_data->main_loop);
	p_data->context= pa_context_new(p_data->mainloop_api, "bar");
	pa_context_connect(p_data->context, 0, PA_CONTEXT_NOFAIL, 0);
	pa_context_set_state_callback(p_data->context, context_change_cb, p_data);
	pa_threaded_mainloop_start(p_data->main_loop);
	pa_threaded_mainloop_unlock(p_data->main_loop);
	while(!p_data->pulse_ready){
		usleep(1);
	}
}

void deactivate_pulse(pulse_data* p_data){
	p_data->mainloop_api->quit(p_data->mainloop_api,0);
	pa_threaded_mainloop_stop(p_data->main_loop);
	pa_threaded_mainloop_free(p_data->main_loop);
}

void* get_sink_volume(void* thread_data){
	pulse_data* p_data=((pulse_data*)thread_data);
	pa_context_get_sink_info_by_name(p_data->context,"alsa_output.pci-0000_04_00.6.analog-stereo",info_cb,p_data);
	while(p_data->vol==-1){
		usleep(1);
	}
	return 0;
}

void append_rate(char* output,int power, int use_bytes){
	if(use_bytes){
		switch(power){
			case 0:
				strcat(output," bytes/s");
				break;
			case 1:
				strcat(output," Kbytes/s");
				break;
			case 2:
				strcat(output," Mbytes/s");
				break;
		}
	}else{
		switch(power){
			case 0:
				strcat(output," bits/s");
				break;
			case 1:
				strcat(output," Kbits/s");
				break;
			case 2:
				strcat(output," Mbits/s");
				break;
		}
	}
}

void* get_network_stats(void* thread_data){
	int use_bytes=0;
	network_thread_data* net_data =(network_thread_data*)thread_data;
	char* output=net_data->network_output;
	memset(net_data->network_output, 0, 100);

	uint64_t rx_bytes_start=0;
	uint64_t tx_bytes_start=0;
	uint64_t rx_packets_start=0;
	uint64_t tx_packets_start=0;

	uint64_t rx_bytes_end=0;
	uint64_t tx_bytes_end=0;
	uint64_t rx_packets_end=0;
	uint64_t tx_packets_end=0;

	fscanf(net_data->rx_bytes_f, "%lu",&rx_bytes_start);
	fscanf(net_data->tx_bytes_f, "%lu",&tx_bytes_start);
	fscanf(net_data->rx_packets_f, "%lu",&rx_packets_start);
	fscanf(net_data->tx_packets_f, "%lu",&tx_packets_start);

	usleep(SLEEP_TIME);

	fseek(net_data->rx_bytes_f, 0, SEEK_SET);
	fseek(net_data->tx_bytes_f, 0, SEEK_SET);
	fseek(net_data->rx_packets_f, 0, SEEK_SET);
	fseek(net_data->tx_packets_f, 0, SEEK_SET);

	fscanf(net_data->rx_bytes_f, "%lu",&rx_bytes_end);
	fscanf(net_data->tx_bytes_f, "%lu",&tx_bytes_end);
	fscanf(net_data->rx_packets_f, "%lu",&rx_packets_end);
	fscanf(net_data->tx_packets_f, "%lu",&tx_packets_end);

	fseek(net_data->rx_bytes_f, 0, SEEK_SET);
	fseek(net_data->tx_bytes_f, 0, SEEK_SET);
	fseek(net_data->rx_packets_f, 0, SEEK_SET);
	fseek(net_data->tx_packets_f, 0, SEEK_SET);


	float rx_bytes_dx=rx_bytes_end-rx_bytes_start;
	float tx_bytes_dx=tx_bytes_end-tx_bytes_start;
	uint32_t rx_packets_dx=rx_packets_end-rx_packets_start;
	uint32_t tx_packets_dx=tx_packets_end-tx_packets_start;

	if(!use_bytes){
		//convert bytes to bits
		rx_bytes_dx*=8;
		tx_bytes_dx*=8;
	}

	int powr_rx=0;
	if(rx_bytes_dx>1024){
		rx_bytes_dx/=1024;
		powr_rx++;
	}
	if(rx_bytes_dx>1024){
		rx_bytes_dx/=1024;
		powr_rx++;
	}

	int powr_tx=0;
	if(tx_bytes_dx>1024){
		tx_bytes_dx/=1024;
		powr_tx++;
	}
	if(tx_bytes_dx>1024){
		tx_bytes_dx/=1024;
		powr_tx++;
	}

	char temp_char[100]={0};
	sprintf(temp_char, "%3.2f",rx_bytes_dx);
	strcat(output,temp_char);


	memset(temp_char, 0, 99);
	sprintf(temp_char, "%d",rx_packets_dx);
	append_rate(output, powr_rx, use_bytes);
	strcat(output,"[");
	strcat(output,temp_char);
	strcat(output,"]");

	strcat(output," ");

	sprintf(temp_char, "%3.2f",tx_bytes_dx);
	strcat(output,temp_char);
	append_rate(output, powr_tx, use_bytes);
	memset(temp_char, 0, 99);
	sprintf(temp_char, "%d",tx_packets_dx);
	strcat(output,"[");
	strcat(output,temp_char);
	strcat(output,"]");
	return 0;
}

void* get_cpu_load(void* thread_data){
	cpu_usage_thread_data* cpu_data=(cpu_usage_thread_data*)thread_data;
	while(1){
		char buf[200]={0};
		uint64_t start_user=0;
		uint64_t start_nice=0;
		uint64_t start_system_=0;
		uint64_t start_idle=0;
		uint64_t start_iowait=0;
		uint64_t start_irq=0;
		uint64_t start_softirq=0;
		uint64_t start_steal=0;
		uint64_t start_guest=0;
		uint64_t start_guest_nice=0;

		uint64_t end_user=0;
		uint64_t end_nice=0;
		uint64_t end_system_=0;
		uint64_t end_idle=0;
		uint64_t end_iowait=0;
		uint64_t end_irq=0;
		uint64_t end_softirq=0;
		uint64_t end_steal=0;
		uint64_t end_guest=0;
		uint64_t end_guest_nice=0;

		read(cpu_data->stat_fd,buf,200);
		sscanf(buf, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu"
				,&start_user,&start_nice,&start_system_,&start_idle,&start_iowait,&start_irq,&start_softirq,&start_steal,&start_guest,&start_guest_nice);
		lseek(cpu_data->stat_fd, 0, SEEK_SET);

		usleep(SLEEP_TIME);

		read(cpu_data->stat_fd,buf,200);
		sscanf(buf, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu"
				,&end_user,&end_nice,&end_system_,&end_idle,&end_iowait,&end_irq,&end_softirq,&end_steal,&end_guest,&end_guest_nice);
		lseek(cpu_data->stat_fd, 0, SEEK_SET);

		uint64_t dx_user=end_user-start_user;
		uint64_t dx_nice=end_nice-start_nice;
		uint64_t dx_system_=end_system_-start_system_;
		uint64_t dx_idle=end_idle-start_idle;
		uint64_t dx_iowait=end_iowait-start_iowait;
		uint64_t dx_irq=end_irq-start_irq;
		uint64_t dx_softirq=end_softirq-start_softirq;
		uint64_t dx_steal=end_steal-start_steal;
		uint64_t dx_guest=end_guest-start_guest;
		uint64_t dx_guest_nice=end_guest_nice-start_guest_nice;

		double sum=dx_user+dx_nice+dx_system_+dx_idle+dx_iowait+dx_irq+dx_softirq+dx_steal+dx_guest+dx_guest_nice;
		float ratio=((double)dx_user)/sum *100;
		cpu_data->ratio=ratio;
	}
	return 0;
}

void get_cpu_freqs(FILE* cpuinfo_f,int* cpu0f,int* cpu1f,int* cpu2f,int* cpu3f){

	fseek(cpuinfo_f, 0, SEEK_SET);
	char buff[100]={0};
	for(int i=0;i<8;i++){
		fgets(buff, 100, cpuinfo_f);
	}

	sscanf(buff,"%*s%*s%*s%d",cpu0f);

	for(int i=0;i<35;i++){
		fgets(buff, 100, cpuinfo_f);
	}

	sscanf(buff,"%*s%*s%*s%d",cpu1f);

	for(int i=0;i<35;i++){
		fgets(buff, 100, cpuinfo_f);
	}

	sscanf(buff,"%*s%*s%*s%d",cpu2f);

	for(int i=0;i<35;i++){
		fgets(buff, 100, cpuinfo_f);
	}

	sscanf(buff,"%*s%*s%*s%d",cpu3f);
}

void get_used_memory(int meminfo_fd, uint32_t* mem_used ){
	char buff[400]={0};
	uint32_t memTotal=0;
	uint32_t memFree=0;
	uint32_t buffers=0;
	uint32_t cached=0;
	int read_ret=read(meminfo_fd,buff,400);
	sscanf(buff,"%*s%d%*s%*s%d%*s%*s%*d%*s%*s%d%*s%*s%d"
			,&memTotal,&memFree,&buffers,&cached);
	uint32_t mem_used_temp=memTotal-memFree-buffers-cached;
	mem_used_temp=mem_used_temp/1024;
	*mem_used=mem_used_temp;
	int seek_ret=lseek(meminfo_fd, 0, SEEK_SET);
}

void get_time(char* date_str){
	memset(date_str,0,100);
	struct	tm result;
	time_t current_time=(unsigned long)time(0);
	localtime_r(&current_time, &result);
	strftime(date_str, 100,"%a %d/%m/%y %H:%M", &result);
}

void get_capacity_and_pow(int capacity_fd,int pow_fd,int* capacity,float* powr){
	char buf[100]={0};
	read(capacity_fd,buf,100);
	sscanf(buf, "%d",capacity);
	read(pow_fd,buf,100);
	sscanf(buf, "%f",powr);
	*powr=*powr/1000000;
	lseek(capacity_fd, 0, SEEK_SET);
	lseek(pow_fd, 0, SEEK_SET);
}

void get_brightness(int brightness_fd,float* brightness){
	char buf[100]={0};
	int ret_int=read(brightness_fd,buf,100);
	sscanf(buf, "%f",brightness);
	*brightness=(*brightness/255)*100;
	lseek(brightness_fd, 0, SEEK_SET);
}

void get_governor(int governor_fd,char* gov){
	int ret_int=read(governor_fd,gov,3);
	lseek(governor_fd, 0, SEEK_SET);
}
	
void get_temp(int temp_fd,float* tempf){
	int temp=0;
	char buf[100]={0};
	int ret_int=read(temp_fd,buf,100);
	sscanf(buf, "%d",&temp);
	*tempf=(float)temp/1000;
	lseek(temp_fd, 0, SEEK_SET);
}

int main(){

	pthread_t audio_thr,net_thr,cpu_thr;

	network_thread_data net_data={0};
	cpu_usage_thread_data cpu_data={0};

	net_data.rx_bytes_f = fopen("/sys/class/net/wlan0/statistics/rx_bytes", "r");
	net_data.tx_bytes_f = fopen("/sys/class/net/wlan0/statistics/tx_bytes", "r");
	net_data.rx_packets_f = fopen("/sys/class/net/wlan0/statistics/rx_packets", "r");
	net_data.tx_packets_f = fopen("/sys/class/net/wlan0/statistics/tx_packets", "r");

	cpu_data.stat_fd = open("/proc/stat", O_RDONLY);

	FILE* cpuinfo_f = fopen("/proc/cpuinfo", "r");
	int temp_fd =open("/sys/class/hwmon/hwmon3/temp1_input",O_RDONLY);
	int meminfo_fd =open("/proc/meminfo",O_RDONLY);
	int governor_fd =open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",O_RDONLY);
	int capacity_fd =open("/sys/class/power_supply/BAT0/capacity",O_RDONLY);
	int pow_fd =open("/sys/class/power_supply/BAT0/power_now",O_RDONLY);
	int brightness_fd = open("/sys/class/backlight/amdgpu_bl0/brightness", O_RDONLY);

	int volume=0;
	char buff[100]={0};
	int cpu0f=0;
	int cpu1f=0;
	int cpu2f=0;
	int cpu3f=0;
	uint32_t mem_used=0;
	char gov[4]={0};
	int capacity=0;
	float pow_now=0;
	float tempf=0;
	float brightness=0;
	char date_str[100];

	pulse_data p_data={0};
	pulse_init(&p_data);

//	volume
	pthread_create(&net_thr, NULL, get_network_stats, (void*)&net_data);
	pthread_create(&cpu_thr, NULL, get_cpu_load, (void*)&cpu_data);

	while(1){
		p_data.vol=-1;
		pthread_create(&audio_thr, NULL, get_sink_volume, (void*)&p_data);
		get_cpu_freqs(cpuinfo_f, &cpu0f, &cpu1f, &cpu2f, &cpu3f);
		get_temp(temp_fd,&tempf);
		get_used_memory(meminfo_fd, &mem_used);
		get_governor(governor_fd,gov);
		get_capacity_and_pow(capacity_fd,pow_fd,&capacity,&pow_now);
		get_brightness(brightness_fd,&brightness);
		get_time(date_str);

		pthread_join(audio_thr, 0);
		/*pthread_join(net_thr, 0);*/
		/*pthread_join(cpu_thr, 0);*/
		//	final print
		printf("%d%% | %.2f | +%.1f°C | %d %d %d %d %s | %d MB | %s | %d %.2fW | %.0f%% | %s \n"
				,p_data.vol,cpu_data.ratio,tempf,cpu0f,cpu1f,cpu2f,cpu3f,gov,mem_used,net_data.network_output,capacity,pow_now,floorf(brightness),date_str);
		usleep(100000);
	}

	deactivate_pulse(&p_data);
}
