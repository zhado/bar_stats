#include <bits/types.h>
#include <linux/limits.h>
#include <pulse/context.h>
#include <pulse/def.h>
#include <pulse/subscribe.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <pulse/pulseaudio.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>

int SLEEP_TIME=600000;
int running=0;

typedef struct network_thread_data{
	char network_output[100];
	int rx_bytes_fd;
	int tx_bytes_fd;
	int rx_packets_fd;
	int tx_packets_fd;
	float rx_bytes_dx;
	float tx_bytes_dx;
	uint32_t rx_packets_dx;
	uint32_t tx_packets_dx;
}network_thread_data;

typedef struct cpu_usage_thread_data{
	int stat_fd;
	float ratio;
}cpu_usage_thread_data;

typedef struct cpu_freq_data{
	int cpu0_fd;
	int cpu1_fd;
	int cpu2_fd;
	int cpu3_fd;
	int cpu0_freq;
	int cpu1_freq;
	int cpu2_freq;
	int cpu3_freq;
	uint32_t sleep_time;
	float ratio;
}cpu_freq_data;

typedef struct pulse_data{
	pa_threaded_mainloop* main_loop;
	pa_mainloop_api* mainloop_api;
	pa_context* context;
	int pulse_ready;
	int done;
	int muted;
	int vol;
}pulse_data;

typedef struct power_data{
	int capacity_fd;
	int pow_fd;
	int charging_fd;

	int capacity;
	float pow_now;
	float pow_full;
	char charging;
	int hours_left;
	int minutes_left;

	char* output_buffer;
}power_data;

void reset_fp(FILE* ptr){
	fseek( ptr, 0, SEEK_END );
	rewind(ptr);
}

void info_cb (pa_context *c, const pa_sink_info *i, int eol, void *userdata){
	pulse_data* p_data=((pulse_data*)userdata);
	if(eol)
		return;
	float vol=((float)i->volume.values[0])/((float)PA_VOLUME_NORM)*100;
	if(i->mute){
		((pulse_data*)userdata)->muted=1;
	}else{
		((pulse_data*)userdata)->muted=0;
	}
	((pulse_data*)userdata)->vol=vol;

	pa_threaded_mainloop_signal(p_data->main_loop, 0);
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

void* get_sink_volume(void* thread_data){
	pulse_data* p_data=((pulse_data*)thread_data);
	p_data->vol=-1;
	p_data->muted=0;
	p_data->done=0;
	pa_operation* op=pa_context_get_sink_info_by_name(p_data->context,"alsa_output.pci-0000_04_00.6.analog-stereo",info_cb,p_data);
	pa_operation_state_t state=pa_operation_get_state(op);

	/*printf("state=%d\n",state);*/
	while(p_data->vol==-1){
		if(state==PA_OPERATION_RUNNING)
			break;
		/*return 0;*/
		usleep(1);
	}
	p_data->done=1;
	pa_operation_unref(op);
	return 0;
}

void suc_cb (pa_context *c, int success, void *userdata){}

void my_subscription_callback(pa_context *c, pa_subscription_event_type_t t,uint32_t idx, void *userdata) {
	get_sink_volume(userdata);
}
void pulse_init(pulse_data* p_data){
	p_data->pulse_ready=0;
	p_data->vol=-1;
	p_data->muted=0;
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
	pa_context_subscribe(p_data->context, PA_SUBSCRIPTION_MASK_SINK,suc_cb, p_data);
	pa_context_set_subscribe_callback (p_data->context,my_subscription_callback,p_data);
}

void deactivate_pulse(pulse_data* p_data){
	p_data->mainloop_api->quit(p_data->mainloop_api,0);
	pa_threaded_mainloop_stop(p_data->main_loop);
	pa_threaded_mainloop_free(p_data->main_loop);
}

void append_rate(char* output,int power, int use_bytes){
	if(use_bytes){
		switch(power){
			case 0:
				strcat(output," B");
				break;
			case 1:
				strcat(output," KB");
				break;
			case 2:
				strcat(output," MB");
				break;
		}
	}else{
		switch(power){
			case 0:
				strcat(output," b");
				break;
			case 1:
				//kilobit
				strcat(output," kb");
				break;
			case 2:
				strcat(output," mb");
				break;
		}
	}
}

void make_net_output(network_thread_data* net_data){
	int use_bytes=0;
	char* output=net_data->network_output;
	if(!use_bytes){
		//convert bytes to bits
		net_data->rx_bytes_dx*=8;
		net_data->tx_packets_dx*=8;
	}

	int powr_rx=0;
	if(net_data->rx_bytes_dx>1024){
		net_data->rx_bytes_dx/=1024;
		powr_rx++;
	}
	if(net_data->rx_bytes_dx>1024){
		net_data->rx_bytes_dx/=1024;
		powr_rx++;
	}

	int powr_tx=0;
	if(net_data->tx_bytes_dx>1024){
		net_data->tx_bytes_dx/=1024;
		powr_tx++;
	}
	if(net_data->tx_bytes_dx>1024){
		net_data->tx_bytes_dx/=1024;
		powr_tx++;
	}

	char temp_char[100]={0};
	sprintf(temp_char, "%3.2f",net_data->rx_bytes_dx);
	strcat(output,temp_char);

	memset(temp_char, 0, 99);
	sprintf(temp_char, "%d",net_data->rx_packets_dx);
	append_rate(output, powr_rx, use_bytes);
	strcat(output,"[");
	strcat(output,temp_char);
	strcat(output,"]");

	strcat(output," ");

	sprintf(temp_char, "%3.2f",net_data->tx_bytes_dx);
	strcat(output,temp_char);
	append_rate(output, powr_tx, use_bytes);
	memset(temp_char, 0, 99);
	sprintf(temp_char, "%d",net_data->tx_packets_dx);
	strcat(output,"[");
	strcat(output,temp_char);
	strcat(output,"]");
}

void* get_network_stats(void* thread_data){
	int use_bytes=0;
	network_thread_data* net_data =(network_thread_data*)thread_data;
	char* output=net_data->network_output;
	char buf[100]={0};
	uint64_t rx_bytes_start=0;
	uint64_t tx_bytes_start=0;
	uint64_t rx_packets_start=0;
	uint64_t tx_packets_start=0;

	uint64_t rx_bytes_end=0;
	uint64_t tx_bytes_end=0;
	uint64_t rx_packets_end=0;
	uint64_t tx_packets_end=0;
	read(net_data->rx_bytes_fd,buf,100);
	sscanf(buf, "%lu",&rx_bytes_start);

	read(net_data->tx_bytes_fd,buf,100);
	sscanf(buf, "%lu",&tx_bytes_start);

	read(net_data->rx_packets_fd,buf,100);
	sscanf(buf, "%lu",&rx_packets_start);

	read(net_data->tx_packets_fd,buf,100);
	sscanf(buf, "%lu",&tx_packets_start);

	lseek(net_data->rx_bytes_fd, 0, SEEK_SET);
	lseek(net_data->tx_bytes_fd, 0, SEEK_SET);
	lseek(net_data->rx_packets_fd, 0, SEEK_SET);
	lseek(net_data->tx_packets_fd, 0, SEEK_SET);
	while(running){
		usleep(SLEEP_TIME*2);
		read(net_data->rx_bytes_fd,buf,100);
		sscanf(buf, "%lu",&rx_bytes_end);

		read(net_data->tx_bytes_fd,buf,100);
		sscanf(buf, "%lu",&tx_bytes_end);

		read(net_data->rx_packets_fd,buf,100);
		sscanf(buf, "%lu",&rx_packets_end);

		read(net_data->tx_packets_fd,buf,100);
		sscanf(buf, "%lu",&tx_packets_end);

		lseek(net_data->rx_bytes_fd, 0, SEEK_SET);
		lseek(net_data->tx_bytes_fd, 0, SEEK_SET);
		lseek(net_data->rx_packets_fd, 0, SEEK_SET);
		lseek(net_data->tx_packets_fd, 0, SEEK_SET);

		net_data->rx_bytes_dx=rx_bytes_end-rx_bytes_start;
		net_data->tx_bytes_dx=tx_bytes_end-tx_bytes_start;
		net_data->rx_packets_dx=rx_packets_end-rx_packets_start;
		net_data->tx_packets_dx=tx_packets_end-tx_packets_start;
		rx_bytes_start=rx_bytes_end;
		tx_bytes_start=tx_bytes_end;
		rx_packets_start=rx_packets_end;
		tx_packets_start=tx_packets_end;

		memset(net_data->network_output, 0, 100);
		make_net_output(net_data);
		int a=10;
	}
	return 0;
}

void* get_cpu_load(void* thread_data){
	while(running){
		cpu_usage_thread_data* cpu_data=(cpu_usage_thread_data*)thread_data;
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

void* get_cpu_freqs(void * thread_data){
	cpu_freq_data* cpu_freqs_data=(cpu_freq_data*)thread_data;
	while(1){
		char buff[100]={0};
		lseek(cpu_freqs_data->cpu0_fd, 0, SEEK_SET);
		read(cpu_freqs_data->cpu0_fd,buff,100);
		sscanf(buff,"%d",&cpu_freqs_data->cpu0_freq);
		memset(buff, 0, 100);

		lseek(cpu_freqs_data->cpu1_fd, 0, SEEK_SET);
		read(cpu_freqs_data->cpu1_fd,buff,100);
		sscanf(buff,"%d",&cpu_freqs_data->cpu1_freq);
		memset(buff, 0, 100);

		lseek(cpu_freqs_data->cpu2_fd, 0, SEEK_SET);
		read(cpu_freqs_data->cpu2_fd,buff,100);
		sscanf(buff,"%d",&cpu_freqs_data->cpu2_freq);
		memset(buff, 0, 100);

		lseek(cpu_freqs_data->cpu3_fd, 0, SEEK_SET);
		read(cpu_freqs_data->cpu3_fd,buff,100);
		sscanf(buff,"%d",&cpu_freqs_data->cpu3_freq);
		cpu_freqs_data->cpu0_freq/=1000;
		cpu_freqs_data->cpu1_freq/=1000;
		cpu_freqs_data->cpu2_freq/=1000;
		cpu_freqs_data->cpu3_freq/=1000;
		usleep(cpu_freqs_data->sleep_time);
	}
	return 0;
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

void* get_capacity_and_pow(void* thread_data){
	power_data* pow_data=(power_data*)thread_data;
	pow_data->pow_full/=1000000;
	char* out_buf=pow_data->output_buffer;
	while(1){
		memset(out_buf, 0, 100);
		char buf[100]={0};

		read(pow_data->capacity_fd,buf,100);
		sscanf(buf, "%d",&pow_data->capacity);
		read(pow_data->pow_fd,buf,100);
		sscanf(buf, "%f",&pow_data->pow_now);
		pow_data->pow_now=pow_data->pow_now/1000000;
		read(pow_data->charging_fd,&pow_data->charging,1);

		float capacity=pow_data->capacity;
		float energy_full=pow_data->pow_full;
		float pow_now=pow_data->pow_now;
		
		sprintf(out_buf, "%d %.2f",pow_data->capacity,pow_data->pow_now);

		if(pow_data->charging=='C'){
			strcat(out_buf, "+");
			float hours_f=(energy_full*(1-capacity/100)/pow_now);
			pow_data->hours_left=(int)hours_f;
			pow_data->minutes_left=(hours_f-(int)hours_f)*60;
		}else{
			strcat(out_buf, "-");
			float hours_f=(energy_full*(capacity/100)/pow_now);
			pow_data->hours_left=(int)hours_f;
			pow_data->minutes_left=(hours_f-(int)hours_f)*60;
		}

		if(pow_data->hours_left!=0){
			memset(buf, 0, 100);
			sprintf(buf, "%d",pow_data->hours_left);
			strcat(out_buf, " ");
			strcat(out_buf, buf);
			strcat(out_buf, "h");
		}

		if(pow_data->minutes_left!=0){
			memset(buf, 0, 100);
			sprintf(buf, "%d",pow_data->minutes_left);
			strcat(out_buf, " ");
			strcat(out_buf, buf);
			strcat(out_buf, "m");
		}

		lseek(pow_data->charging_fd, 0, SEEK_SET);
		lseek(pow_data->capacity_fd, 0, SEEK_SET);
		lseek(pow_data->pow_fd, 0, SEEK_SET);
		usleep(SLEEP_TIME);
	}
	return 0;
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
	running=1;
	pthread_t audio_thr,net_thr,cpu_load_thr,pow_thr,cpu_freqs_thr;

	network_thread_data net_data={0};
	cpu_usage_thread_data cpu_data={0};
	cpu_freq_data cpu_freqs_data={0};
	pulse_data p_data={0};
	power_data pow_data={0};
	char pow_buff[100]={0};
	pow_data.output_buffer=pow_buff;

	net_data.rx_bytes_fd = open("/sys/class/net/wlan0/statistics/rx_bytes", O_RDONLY);
	net_data.tx_bytes_fd = open("/sys/class/net/wlan0/statistics/tx_bytes", O_RDONLY);
	net_data.rx_packets_fd = open("/sys/class/net/wlan0/statistics/rx_packets", O_RDONLY);
	net_data.tx_packets_fd = open("/sys/class/net/wlan0/statistics/tx_packets", O_RDONLY);

	cpu_data.stat_fd = open("/proc/stat", O_RDONLY);

	cpu_freqs_data.cpu0_fd = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", O_RDONLY);
	cpu_freqs_data.cpu1_fd = open("/sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq", O_RDONLY);
	cpu_freqs_data.cpu2_fd = open("/sys/devices/system/cpu/cpu2/cpufreq/scaling_cur_freq", O_RDONLY);
	cpu_freqs_data.cpu3_fd = open("/sys/devices/system/cpu/cpu3/cpufreq/scaling_cur_freq", O_RDONLY);
	cpu_freqs_data.sleep_time=450000;


	/*FILE* cpuinfo_f = fopen("/proc/cpuinfo", "r");*/
	int temp_fd =open("/sys/class/hwmon/hwmon3/temp1_input",O_RDONLY);
	int meminfo_fd =open("/proc/meminfo",O_RDONLY);
	int governor_fd =open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",O_RDONLY);
	pow_data.capacity_fd =open("/sys/class/power_supply/BAT0/capacity",O_RDONLY);
	pow_data.pow_fd =open("/sys/class/power_supply/BAT0/power_now",O_RDONLY);
	pow_data.charging_fd =open("/sys/class/power_supply/BAT0/status",O_RDONLY);
	int energy_full_fd=open("/sys/class/power_supply/BAT0/energy_full",O_RDONLY);
	int brightness_fd = open("/sys/class/backlight/amdgpu_bl0/brightness", O_RDONLY);

	int pipe_fd_read = open("./aq", O_RDONLY | O_NONBLOCK);
	int pipe_fd = open("./aq", O_WRONLY | O_NONBLOCK);
	close(pipe_fd_read);
	signal(SIGPIPE, SIG_IGN);

	int volume=0;
	int cpu0f=0;
	int cpu1f=0;
	int cpu2f=0;
	int cpu3f=0;
	uint32_t mem_used=0;
	char gov[4]={0};
	float tempf=0;
	float brightness=0;
	char date_str[100];

	pulse_init(&p_data);
	get_sink_volume(&p_data);
	pthread_create(&net_thr, NULL, get_network_stats, (void*)&net_data);
	pthread_create(&cpu_load_thr, NULL, get_cpu_load, (void*)&cpu_data);
	pthread_create(&pow_thr, NULL, get_capacity_and_pow, (void*)&pow_data);
	pthread_create(&cpu_freqs_thr, NULL, get_cpu_freqs, (void*)&cpu_freqs_data);

	
	char buff[100]={0};
	read(energy_full_fd,buff,100);
	sscanf(buff, "%f",&pow_data.pow_full);
	close(energy_full_fd);

	while(1){
		char buff[100]={0};
		char print_buff[200]={0};
		snprintf(buff,100, "%d%%",p_data.vol);
		if(p_data.muted){
			strcat(buff," M");
		}
		get_temp(temp_fd,&tempf);
		get_used_memory(meminfo_fd, &mem_used);
		get_governor(governor_fd,gov);
		get_brightness(brightness_fd,&brightness);
		get_time(date_str);

		sprintf(print_buff,"%s | %.2f | +%.1f°C | %d %d %d %d %s | %d MB | %s | %s | %.0f%% | %s \n"
				,buff,cpu_data.ratio,tempf,cpu_freqs_data.cpu0_freq,cpu_freqs_data.cpu1_freq,cpu_freqs_data.cpu2_freq,cpu_freqs_data.cpu3_freq,gov,
				mem_used,net_data.network_output,pow_data.output_buffer,floorf(brightness),date_str);
		printf("%s",print_buff);

		write(pipe_fd,print_buff,200);
		fflush(stdout);
		usleep(100000);
	}

}
