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

int SLEEP_TIME=1000000;

void info_cb (pa_context *c, const pa_sink_info *i, int eol, void *userdata){
	if(eol)
		return;
	float vol=((float)i->volume.values[0])/((float)PA_VOLUME_NORM)*100;
	*((float*)userdata)=vol;
}

void context_change_cb(pa_context *c, void* userdata){
	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_READY:{
			pa_context_get_sink_info_by_name(c,"alsa_output.pci-0000_04_00.6.analog-stereo",info_cb,userdata);
		}break;
		default:
		break;
	}
}

void* get_sink_volume(void* thread_data){
	pa_threaded_mainloop* main_loop=pa_threaded_mainloop_new();

	pa_threaded_mainloop_lock(main_loop);
	pa_mainloop_api* mainloop_api = pa_threaded_mainloop_get_api(main_loop);
	pa_context* context = pa_context_new(mainloop_api, "bar");
	float vol=0;

	pa_context_connect(context, 0, PA_CONTEXT_NOFAIL, 0);
	pa_context_set_state_callback(context, context_change_cb, &vol);
	pa_threaded_mainloop_start(main_loop);
	pa_threaded_mainloop_unlock(main_loop);
	while(!vol){
		usleep(1);
	}

	mainloop_api->quit(mainloop_api,0);
	pa_threaded_mainloop_stop(main_loop);
	pa_threaded_mainloop_free(main_loop);
	*((int*)thread_data)=(int)ceilf(vol);
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
	char* output=(char*)thread_data;
	FILE* rx_bytes_f = fopen("/sys/class/net/wlan0/statistics/rx_bytes", "r");
	FILE* tx_bytes_f = fopen("/sys/class/net/wlan0/statistics/tx_bytes", "r");
	FILE* rx_packets_f = fopen("/sys/class/net/wlan0/statistics/rx_packets", "r");
	FILE* tx_packets_f = fopen("/sys/class/net/wlan0/statistics/tx_packets", "r");

	uint64_t rx_bytes_start=0;
	uint64_t tx_bytes_start=0;
	uint64_t rx_packets_start=0;
	uint64_t tx_packets_start=0;

	uint64_t rx_bytes_end=0;
	uint64_t tx_bytes_end=0;
	uint64_t rx_packets_end=0;
	uint64_t tx_packets_end=0;

	fscanf(rx_bytes_f, "%lu",&rx_bytes_start);
	fscanf(tx_bytes_f, "%lu",&tx_bytes_start);
	fscanf(rx_packets_f, "%lu",&rx_packets_start);
	fscanf(tx_packets_f, "%lu",&tx_packets_start);

	usleep(SLEEP_TIME);

	fseek(rx_bytes_f, 0, SEEK_SET);
	fseek(tx_bytes_f, 0, SEEK_SET);
	fseek(rx_packets_f, 0, SEEK_SET);
	fseek(tx_packets_f, 0, SEEK_SET);

	fscanf(rx_bytes_f, "%lu",&rx_bytes_end);
	fscanf(tx_bytes_f, "%lu",&tx_bytes_end);
	fscanf(rx_packets_f, "%lu",&rx_packets_end);
	fscanf(tx_packets_f, "%lu",&tx_packets_end);

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
	FILE* stat = fopen("/proc/stat", "r");
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

	fscanf(stat, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu"
			,&start_user,&start_nice,&start_system_,&start_idle,&start_iowait,&start_irq,&start_softirq,&start_steal,&start_guest,&start_guest_nice);
	fseek(stat, 0, SEEK_SET);

	usleep(SLEEP_TIME);

	fscanf(stat, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu"
			,&end_user,&end_nice,&end_system_,&end_idle,&end_iowait,&end_irq,&end_softirq,&end_steal,&end_guest,&end_guest_nice);

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
	*((float*)thread_data)=ratio;
	return 0;
}

int main(){

	pthread_t audio_thr,net_thr,cpu_thr;
//	volume
	int volume=0;
	char network_output[100]={0};
	float cpu_load=0;
	pthread_create( &audio_thr, NULL, get_sink_volume, (void*)&volume);
	pthread_create( &net_thr, NULL, get_network_stats, (void*)&network_output);
	pthread_create( &cpu_thr, NULL, get_cpu_load, (void*)&cpu_load);

//	cpu

	FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
	char buff[100];
	for(int i=0;i<8;i++){
		fgets(buff, 100, cpuinfo);
	}

	int cpu0f=0;
	sscanf(buff,"%*s%*s%*s%d",&cpu0f);

	for(int i=0;i<35;i++){
		fgets(buff, 100, cpuinfo);
	}

	int cpu1f=0;
	sscanf(buff,"%*s%*s%*s%d",&cpu1f);

	for(int i=0;i<35;i++){
		fgets(buff, 100, cpuinfo);
	}

	int cpu2f=0;
	sscanf(buff,"%*s%*s%*s%d",&cpu2f);

	for(int i=0;i<35;i++){
		fgets(buff, 100, cpuinfo);
	}

	int cpu3f=0;
	sscanf(buff,"%*s%*s%*s%d",&cpu3f);

//  	temp


	int temp=0;
	FILE* temp_f = fopen("/sys/class/hwmon/hwmon3/temp1_input", "r");
	fscanf(temp_f, "%d",&temp);
	float tempf=(float)temp/1000;

//  	memory
	
	__uint32_t memTotal=0;
	FILE* meminfo = fopen("/proc/meminfo", "r");
	fgets(buff, 100, meminfo);
	sscanf(buff,"%*s%d",&memTotal);


	__uint32_t memFree=0;
	fgets(buff, 100, meminfo);
	sscanf(buff,"%*s%d",&memFree);

	
	// skip mmemavialable
	fgets(buff, 100, meminfo);

	__uint32_t buffers=0;
	fgets(buff, 100, meminfo);
	sscanf(buff,"%*s%d",&buffers);


	__uint32_t cached=0;
	fgets(buff, 100, meminfo);
	sscanf(buff,"%*s%d",&cached);

	__uint32_t mem_used=memTotal-memFree-buffers-cached;
	mem_used=mem_used/1024;

//	governor

	char gov[4];
	FILE* governor = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
	fgets(gov, 4, governor);
	/*gov[3]=0;*/
	
// 	bat capacity and discharge rate

	__uint32_t capacity=0;
	FILE* capacity_f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
	fscanf(capacity_f, "%d",&capacity);

	float pow_now=0;
	FILE* pow_f = fopen("/sys/class/power_supply/BAT0/power_now", "r");
	fscanf(pow_f, "%f",&pow_now);
	pow_now=pow_now/1000000;

//	brightness

	int brightness_int=0;
	FILE* brightness_f = fopen("/sys/class/backlight/amdgpu_bl0/brightness", "r");
	fscanf(brightness_f, "%d",&brightness_int);
	float brightness=brightness_int;
	brightness=(brightness/255)*100;
	
//	date_time

	char date_str[100];
	struct	tm result;
	time_t current_time=(unsigned long)time(0);
	localtime_r(&current_time, &result);
	strftime(date_str, 100,"%a %d/%m/%y %H:%M", &result);

	/*get_network_stats(network_output,0);*/
	
//	join threads
	pthread_join(audio_thr, 0);
	pthread_join(net_thr, 0);
	pthread_join(cpu_thr, 0);

//	final print

	printf("%d%% | %.2f | +%.1f°C | %d %d %d %d %s | %d MB | %s | %d %.2fW | %.0f%% | %s \n"
		,volume,cpu_load,tempf,cpu0f,cpu1f,cpu2f,cpu3f,gov,mem_used,network_output,capacity,pow_now,floorf(brightness),date_str);
}
