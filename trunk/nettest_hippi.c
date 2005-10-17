#ifdef lint
#define DO_HIPPI
#define DIRTY
#define INTERVALS
#endif /* lint */
#ifdef DO_HIPPI
char	nettest_hippi[]="\
@(#)nettest_hippi.c (c) Copyright 1994, 1995 Hewlett-Packard Co. Version 2.1";
     
/****************************************************************/
/*								*/
/*	nettest_hippi.c						*/
/*								*/
/*      the HIPPI sockets parsing routine...                    */
/*                                                              */
/*      scan_hippi_args()                                       */
/*                                                              */
/*	the actual test routines...				*/
/*								*/
/*	send_hippi_stream()	perform a hippi stream test	*/
/*	recv_hippi_stream()					*/
/*	send_hippi_rr()		perform a hippi request/response*/
/*	recv_hippi_rr()						*/
/*								*/
/****************************************************************/

 /**********************************************************************/
 /*  WARNING   WARNING WARNING   WARNING  WARNING  WARNING    WARNING  */
 /*                                                                    */
 /* This test uses HP's LLA (Link Level Access) to send and receive    */
 /* packets over the HiPPI interface. LLA is a "dying" access          */
 /* mechansism, and will not be supported past the 9.X release. Do not */
 /* use LLA for other link types. Instead, you should be using DLPI.   */
 /*                                                                    */
 /* These tests would, but HP HiPPI does not yet support DLPI access...*/
 /*                                                                    */
 /*  WARNING   WARNING WARNING   WARNING  WARNING  WARNING    WARNING  */
 /**********************************************************************/
     
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <malloc.h>

#include <netio.h>
#ifndef BUTNOTHIPPI
#include <sys/hippi.h>
#endif

#include "netlib.h"
#include "netsh.h"
#include "nettest_hippi.h"



 /* these variables are specific to the HIPPI sockets tests. declare */
 /* them static to make them global only to this file. some of these */
 /* might not actually have meaning in a HIPPI API test. */

static int	
  req_size,		/* request size                   	*/
  rsp_size,		/* response size			*/
  send_size,		/* how big are individual sends		*/
  recv_size;		/* how big are individual receives	*/

static char
  loc_hippi_device[32],
  rem_hippi_device[32];

static int
  loc_hippi_sap,
  rem_hippi_sap;

static unsigned char
  loc_hippi_mac[6],
  rem_hippi_mac[6];

static int
  loc_recv_bufs,
  rem_recv_bufs;

static int
  recv_flow_control;

static int
  init_done = 0;

char hippi_usage[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
HIPPI Sockets API Test Options:\n\
    -B num_bufs       Set the size of the recv cache \n\
    -D device_path    The path to the HIPPI device\n\
    -F                Request flow control \n\
    -h                Display this text\n\
    -m bytes          Set the send size (HIPPI_STREAM)\n\
    -M bytes          Set the recv size (HIPPI_STREAM)\n\
    -r bytes,bytes    Set request,response size (HIPPI_RR)\n\
    -s sap            Set the local and remote SAPs to sap\n\
\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n"; 
     

 /* this routine will set the default values for all the test specific */
 /* variables. it is declared static so that it is not exported */
 /* outside of the module */
static void
init_test_vars()
{

  if (init_done) {
    return;
  }
  else {

    req_size = 1;
    rsp_size = 1;
    send_size = 0;
    recv_size = 0;
    
    loc_hippi_sap = 84;
    rem_hippi_sap = 84;
    
    strcpy(loc_hippi_device,"/dev/hippi");
    strcpy(rem_hippi_device,"/dev/hippi");
    
    loc_recv_bufs = 0; /* don't change from default */
    rem_recv_bufs = 0; /* don't change from default */

    recv_flow_control = 0;

    init_done = 1;

  }
}

 /* This routine will create a data (listen) socket with the apropriate */
 /* options set and return it to the caller. this replaces all the */
 /* duplicate code in each of the test routines and should help make */
 /* things a little easier to understand. since this routine can be */
 /* called by either the netperf or netserver programs, all output */
 /* should be directed towards "where." when being called from */
 /* netserver, care should be taken to insure that the globals */
 /* referenced by this routine are set. */

int
create_hippi_socket()
{

  int temp_socket;
  struct fis arg;

  /*set up the data socket                        */
  temp_socket = open(loc_hippi_device, O_RDWR);
  if (temp_socket < 0){
    fprintf(where,
	    "netperf: create_hippi_socket: could not open %s: %d\n",
	    loc_hippi_device,
	    errno);
    fflush(where);
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"create_hippi_socket: socket %d obtained...\n",temp_socket);
    fflush(where);
  }
  
  /* now we want to find our local MAC address so we can tell the */
  /* remote about it */

  bzero(&arg,sizeof(arg));
  arg.reqtype = LOCAL_ADDRESS;
  arg.vtype = 6;

  if (ioctl(temp_socket,
	    NETSTAT,
	    &arg) != 0) {
    fprintf(where,
	    "netperf: create_hippi_socket: could not retrieve MAC: errno %d\n",
	    errno);
    fflush(where);
    exit(1);
  }
  memcpy(loc_hippi_mac,arg.value.s,6);

  if (debug) {
    fprintf(where,
	    "create_hippi_socket: local address is "),
    fprintf(where,
	    "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
	    loc_hippi_mac[0],
	    loc_hippi_mac[1],
	    loc_hippi_mac[2],
	    loc_hippi_mac[3],
	    loc_hippi_mac[4],
	    loc_hippi_mac[5]);
    fflush(where);
  }
  

  /* we now want to assign a local sap to this FD and store it in the */
  /* global loc_sap. the server will return his loc_sap and we will */
  /* use it as the rem_sap. for the time-being, we will just set the */
  /* local and remote saps to be the same. however, we will not set */
  /* the remote in this routine because we would ike to preserve the */
  /* illusion that the remote will pick sometihng arbitrary and tell */
  /* us what it is */

  arg.reqtype = LOG_SSAP;
  arg.vtype = INTEGERTYPE;
  arg.value.i = loc_hippi_sap;

  if (ioctl(temp_socket,
	    NETCTRL,
	    &arg) != 0) {
    fprintf(where,
	    "netperf: create_hippi_socket: could not bind sap %d: errno %d",
	    loc_hippi_sap,
	    errno);
    fflush(where);
    exit(1);
  }

  
  if (debug) {
    fprintf(where,"create_hippi_socket: SSAP bound to %d\n",loc_hippi_sap);
    fflush(where);
  }
  

  /* if the user wished to try and change the number of receive */
  /* buffers, do it */

  if (loc_recv_bufs) {
    arg.reqtype = LOG_READ_CACHE;
    arg.vtype = INTEGERTYPE;
    arg.value.i = loc_recv_bufs;

    if (ioctl(temp_socket,
	      NETCTRL,
	      &arg) != 0) {
      fprintf(where,
	      "netperf: create_hippi_socket: error changing read cache: errno %d\n",
	      errno);
      fflush(where);
    }
    
    if (debug) {
      fprintf(where,
	      "create_hippi_socket: num recv bufs set to %d\n",loc_recv_bufs);
      fflush(where);
    }
  }
  
#ifndef BUTNOTHIPPI
  if (recv_flow_control) {
    arg.reqtype = RX_FLOW_CONTROL;
    arg.vtype = INTEGERTYPE;
    arg.value.i = 1;
    if (ioctl(temp_socket,
	      NETCTRL,
	      &arg) != 0) {
      fprintf(where,
	      "netperf: create_hippi_socket: could not enable flow control: errno %d\n",
	      errno);
      fflush(where);
    }
  
    if (debug) {
      fprintf(where,
	      "create_hippi_socket: recv flow control enabled %d\n",
	      loc_hippi_sap);
      fflush(where);
    }
  }
#endif /* BUTNOTHIPPI */

  return(temp_socket);

}



void
send_hippi_stream(remote_host)
char	remote_host[];
{
  /*********************************************************************/
  /*								       */
  /*               	HIPPI Unidirectional Send Test                 */
  /*								       */
  /*********************************************************************/
  char *tput_title =
    "Socket  Message  Elapsed      Messages                \n\
Size    Size     Time         Okay Errors   Throughput\n\
bytes   bytes    secs            #      #   %s/sec\n\n";
  
  char *tput_fmt_0 =
    "%7.2f\n";
  
  char *tput_fmt_1 =
    "%5d   %5d    %-7.2f   %7d %6d    %7.2f\n\
%5d            %-7.2f   %7d           %7.2f\n\n";
  
  
  char *cpu_title =
    "Socket  Message  Elapsed      Messages                   CPU     Service\n\
Size    Size     Time         Okay Errors   Throughput   Util    Demand\n\
bytes   bytes    secs            #      #   %s/sec   %%       us/KB\n\n";
  
  char *cpu_fmt_0 =
    "%6.2f\n";
  
  char *cpu_fmt_1 =
    "%5d   %5d    %-7.2f   %7d %6d    %7.1f      %-6.2f  %-6.3f\n\
%5d            %-7.2f   %7d           %7.1f      %-6.2f  %-6.3f\n\n";

  int 	data_socket;
  struct fis arg;

  float	elapsed_time; 
  float local_cpu_utilization;
  float remote_cpu_utilization;
  float	local_service_demand;
  float remote_service_demand;

  double local_thruput;
  double remote_thruput;
  double bytes_sent;
  double bytes_recvd;
  
  struct ring_elt *send_ring;

  int	len;
  int	*message_int_ptr;
  int	failed_sends;
  int 	messages_sent;
  int	messages_recvd;

  
  
#ifdef INTERVALS
  int	interval_count;
#endif /* INTERVALS */
#ifdef DIRTY
  int	i;
#endif /* DIRTY */
  
  struct        sigaction       action;

  struct	hippi_stream_request_struct	*hippi_stream_request;
  struct	hippi_stream_response_struct	*hippi_stream_response;
  struct	hippi_stream_results_struct	*hippi_stream_results;
  
  init_test_vars();

  hippi_stream_request	= 
    (struct hippi_stream_request_struct *)netperf_request.content.test_specific_data;
  hippi_stream_response	= 
    (struct hippi_stream_response_struct *)netperf_response.content.test_specific_data;
  hippi_stream_results	= 
    (struct hippi_stream_results_struct *)netperf_response.content.test_specific_data;
  
  if ( print_headers ) {
    printf("HIPPI UNIDIRECTIONAL SEND TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      printf(cpu_title,format_units());
    else
      printf(tput_title,format_units());
  }	
  
  failed_sends	= 0;
  messages_sent	= 0;
  times_up	= 0;
  
  /*set up the data socket			*/
  data_socket = create_hippi_socket();
  
  if (data_socket < 0){
    perror("netperf: send_hippi_stream: data socket");
    exit(1);
  }
  
  /* now, we want to see if we need to set the send_size. if the user */
  /* did not specify a send_size on the command line, we will use 4096 */
  /* bytes */
  if (send_size == 0) {
      send_size = 4096;
  }
  
  /* set-up the data buffer with the requested alignment and offset, */
  /* most of the numbers here are just a hack to pick something nice */
  /* and big in an attempt to never try to send a buffer a second time */
  /* behippi it leaves the node...unless the user set the width */
  /* explicitly. */

  if (send_width == 0) send_width = 32;

  send_ring = allocate_buffer_ring(send_width,
				   send_size,
				   local_send_align,
				   local_send_offset);

  /* if the user supplied a cpu rate, this call will complete rather */
  /* quickly, otherwise, the cpu rate will be retured to us for */
  /* possible display. The Library will keep it's own copy of this data */
  /* for use elsewhere. We will only display it. (Does that make it */
  /* "opaque" to us?) */
  
  if (local_cpu_usage)
    local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
  
  /* Tell the remote end to set up the data connection. The server */
  /* sends back the port number and alters the socket parameters there. */
  /* Of course this is a datagram service so no connection is actually */
  /* set up, the server just sets up the socket and binds it. */
  
  netperf_request.content.request_type = DO_HIPPI_STREAM;
  hippi_stream_request->message_size	= send_size;
  hippi_stream_request->recv_alignment	= remote_recv_align;
  hippi_stream_request->recv_offset	= remote_recv_offset;
  hippi_stream_request->measure_cpu	= remote_cpu_usage;
  hippi_stream_request->cpu_rate	= remote_cpu_rate;
  hippi_stream_request->test_length	= test_time;
  hippi_stream_request->dev_name_len    = strlen(rem_hippi_device);
  strcpy(hippi_stream_request->hippi_device,rem_hippi_device);
  hippi_stream_request->recv_flow_control = recv_flow_control;
  hippi_stream_request->client_sap      = loc_hippi_sap;
  hippi_stream_request->server_sap      = rem_hippi_sap;
  hippi_stream_request->rem_recv_bufs   = rem_recv_bufs;
  memcpy(hippi_stream_request->mac_addr,loc_hippi_mac,6);
  send_request();
  
  recv_response();
  
  if (!netperf_response.content.serv_errno) {
    if (debug)
      fprintf(where,"send_hippi_stream: remote data connection done.\n");
  }
  else {
    errno = netperf_response.content.serv_errno;
    perror("send_hippi_stream: error on remote");
    exit(1);
  }
  
  /* take the adressing information returned by the remote and give it */
  /* to the system so it knows where out packets are supposed to go. */
  
  remote_cpu_rate	= hippi_stream_response->cpu_rate;
  rem_hippi_sap         = hippi_stream_response->server_sap;
  
  /* We "connect" up to the remote using a couple of ioctls */
  arg.reqtype = LOG_DSAP;
  arg.vtype = INTEGERTYPE;
  arg.value.i = rem_hippi_sap;

  if (ioctl(data_socket,
	    NETCTRL,
	    &arg) != 0) {
    fprintf(where,
	    "netperf: send_hippi_stream: error binding remote sap %d: errno %d",
	    rem_hippi_sap,
	    errno);
    fflush(where);
    exit(1);
  }
    
  if (debug) {
    fprintf(where,
	    "send_hippi_stream: DSAP bound to %d\n",
	    rem_hippi_sap);
    fflush(where);
  }


  memcpy(arg.value.s,hippi_stream_response->mac_addr,6);
  arg.reqtype = LOG_DEST_ADDR;
  arg.vtype = 6;

  if (ioctl(data_socket,
	    NETCTRL,
	    &arg) != 0) {
    fprintf(where,
	    "netperf: send_hippi_stream: could not bind remote MAC: errno %d\n",
	    errno);
    fflush(where);
    exit(1);
  }
  
  if (debug) {
    fprintf(where,
	    "send_hippi_stream: remote address is "),
    fprintf(where,
	    "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
	    hippi_stream_response->mac_addr[0],
	    hippi_stream_response->mac_addr[1],
	    hippi_stream_response->mac_addr[2],
	    hippi_stream_response->mac_addr[3],
	    hippi_stream_response->mac_addr[4],
	    hippi_stream_response->mac_addr[5]);
    fflush(where);
  }
  
    
  /* set up the timer to call us after test_time	*/
  signal(SIGALRM, catcher);
  start_timer(test_time);
  
  /* Get the start count for the idle counter and the start time */
  
  cpu_start(local_cpu_usage);
  
#ifdef INTERVALS
  interval_count = interval_burst;
#endif
  
  /* Send datagrams like there was no tomorrow. at somepoint it might */
  /* be nice to set this up so that a quantity of bytes could be sent, */
  /* but we still need some sort of end of test trigger on the receive */
  /* side. that could be a select with a one second timeout, but then */
  /* if there is a test where none of the data arrives for awile and */
  /* then starts again, we would end the test too soon. something to */
  /* think about... */
  while (!times_up) {

#ifdef DIRTY
    /* we want to dirty some number of consecutive integers in the buffer */
    /* we are about to send. we may also want to bring some number of */
    /* them cleanly into the cache. The clean ones will follow any dirty */
    /* ones into the cache. */
    message_int_ptr = (int *)(send_ring->buffer_ptr);
    for (i = 0; i < loc_dirty_count; i++) {
      *message_int_ptr = 4;
      message_int_ptr++;
    }
    for (i = 0; i < loc_clean_count; i++) {
      loc_dirty_count = *message_int_ptr;
      message_int_ptr++;
    }
#endif /* DIRTY */

    if ((len=write(data_socket,
		   send_ring->buffer_ptr,
		   send_size))  != send_size) {
      if ((len >= 0) || (errno == EINTR))
	break;
      if (errno == ENOBUFS) {
	/* what is the error message when we are sending too fast? is */
	/* there one? will I be flow controlled? */
	failed_sends++;
	continue;
      }
      perror("hippi_send: data send error");
      exit(1);
    }
    messages_sent++;          
    
    /* now we want to move our pointer to the next position in the */
    /* data buffer... */

    send_ring = send_ring->next;
    
    
#ifdef INTERVALS
    /* in this case, the interval count is the count-down couter */
    /* to decide to sleep for a little bit */
    if ((interval_burst) && (--interval_count == 0)) {
      /* call the sleep routine for some milliseconds, if our */
      /* timer popped while we were in there, we want to */
      /* break out of the loop. */
      if (msec_sleep(interval_wate)) {
	break;
      }
      interval_count = interval_burst;
    }
    
#endif
    
  }
  
  /* This is a timed test, so the remote will be returning to us after */
  /* a time. We should not need to send any "strange" messages to tell */
  /* the remote that the test is completed, unless we decide to add a */
  /* number of messages to the test. */
  
  /* the test is over, so get stats and stuff */
  cpu_stop(local_cpu_usage,	
	   &elapsed_time);
  
  /* Get the statistics from the remote end	*/
  recv_response();
  if (!netperf_response.content.serv_errno) {
    if (debug)
      fprintf(where,"send_hippi_stream: remote results obtained\n");
  }
  else {
    errno = netperf_response.content.serv_errno;
    perror("send_hippi_stream: error on remote");
    exit(1);
  }

  /* The test is over. */
  
  if (close(data_socket) != 0) {
    /* we will not consider this a fatal error. just display a message */
    /* and move on */
    perror("netperf: cannot shutdown hippi socket");
  }
  

  bytes_sent	= send_size * messages_sent;
  local_thruput	= calc_thruput(bytes_sent);
  
  messages_recvd	= hippi_stream_results->messages_recvd;
  bytes_recvd	= send_size * messages_recvd;
  
  /* we asume that the remote ran for as long as we did */
  
  remote_thruput	= calc_thruput(bytes_recvd);
  
  /* print the results for this socket and message size */
  
  if (local_cpu_usage || remote_cpu_usage) {
    /* We must now do a little math for service demand and cpu */
    /* utilization for the system(s) We pass zeros for the local */
    /* cpu utilization and elapsed time to tell the routine to use */
    /* the libraries own values for those. */
    if (local_cpu_usage) {
      if (local_cpu_rate == 0.0) {
	fprintf(where,"WARNING WARNING WARNING  WARNING WARNING WARNING  WARNING!\n");
	fprintf(where,"Local CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      
      local_cpu_utilization	= calc_cpu_util(0.0);
      local_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      0.0,
						      0);
    }
    else {
      local_cpu_utilization	= -1.0;
      local_service_demand	= -1.0;
    }
    
    /* The local calculations could use variables being kept by */
    /* the local netlib routines. The remote calcuations need to */
    /* have a few things passed to them. */
    if (remote_cpu_usage) {
      if (remote_cpu_rate == 0.0) {
	fprintf(where,"DANGER   DANGER  DANGER   DANGER  DANGER   DANGER   DANGER!\n");
	fprintf(where,"REMOTE CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      
      remote_cpu_utilization	= hippi_stream_results->cpu_util;
      remote_service_demand	= calc_service_demand(bytes_recvd,
						      0.0,
						      remote_cpu_utilization,
						      hippi_stream_results->num_cpus);
    }
    else {
      remote_cpu_utilization	= -1.0;
      remote_service_demand	= -1.0;
    }
    
    /* We are now ready to print all the information. If the user */
    /* has specified zero-level verbosity, we will just print the */
    /* local service demand, or the remote service demand. If the */
    /* user has requested verbosity level 1, he will get the basic */
    /* "streamperf" numbers. If the user has specified a verbosity */
    /* of greater than 1, we will display a veritable plethora of */
    /* background information from outside of this block as it it */
    /* not cpu_measurement specific...  */
    
    switch (verbosity) {
    case 0:
      if (local_cpu_usage) {
	fprintf(where,
		cpu_fmt_0,
		local_service_demand);
      }
      else {
	fprintf(where,
		cpu_fmt_0,
		remote_service_demand);
      }
      break;
    case 1:
      fprintf(where,
	      cpu_fmt_1,		/* the format string */
	      loc_recv_bufs,		/* local sendbuf size */
	      send_size,		/* how large were the sends */
	      elapsed_time,		/* how long was the test */
	      messages_sent,
	      failed_sends,
	      local_thruput, 		/* what was the xfer rate */
	      local_cpu_utilization,	/* local cpu */
	      local_service_demand,	/* local service demand */
	      rem_recv_bufs,
	      elapsed_time,
	      messages_recvd,
	      remote_thruput,
	      remote_cpu_utilization,	/* remote cpu */
	      remote_service_demand);	/* remote service demand */
      break;
    }
  }
  else {
    /* The tester did not wish to measure service demand. */
    switch (verbosity) {
    case 0:
      fprintf(where,
	      tput_fmt_0,
	      local_thruput);
      break;
    case 1:
      fprintf(where,
	      tput_fmt_1,		/* the format string */
	      loc_recv_bufs, 		/* local sendbuf size */
	      send_size,		/* how large were the sends */
	      elapsed_time, 		/* how long did it take */
	      messages_sent,
	      failed_sends,
	      local_thruput,
	      rem_recv_bufs, 		/* remote recvbuf size */
	      elapsed_time,
	      messages_recvd,
	      remote_thruput
	      );
      break;
    }
  }
}


 /* this routine implements the receive side (netserver) of the */
 /* HIPPI_STREAM performance test. */

int
recv_hippi_stream()
{
  struct ring_elt *recv_ring;

  int connection_id;

  int	s_data;
  int	len;
  int	bytes_received = 0;
  float	elapsed_time;
  
  int	message_size;
  int	messages_recvd = 0;
  int	measure_cpu;
  
  struct        sigaction     action;

  struct	hippi_stream_request_struct	*hippi_stream_request;
  struct	hippi_stream_response_struct	*hippi_stream_response;
  struct	hippi_stream_results_struct	*hippi_stream_results;
  
  init_test_vars();

  hippi_stream_request  = 
    (struct hippi_stream_request_struct *)netperf_request.content.test_specific_data;
  hippi_stream_response = 
    (struct hippi_stream_response_struct *)netperf_response.content.test_specific_data;
  hippi_stream_results  = 
    (struct hippi_stream_results_struct *)netperf_response.content.test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_hippi_stream: entered...\n");
    fflush(where);
  }
  
  /* We want to set-up the listen socket with all the desired */
  /* parameters and then let the initiator know that all is ready. If */
  /* socket size defaults are to be used, then the initiator will have */
  /* sent us 0's. If the socket sizes cannot be changed, then we will */
  /* send-back what they are. If that information cannot be determined, */
  /* then we send-back -1's for the sizes. If things go wrong for any */
  /* reason, we will drop back ten yards and punt. */
  
  /* If anything goes wrong, we want the remote to know about it. It */
  /* would be best if the error that the remote reports to the user is */
  /* the actual error we encountered, rather than some bogus unexpected */
  /* response type message. */
  
  if (debug > 1) {
    fprintf(where,"recv_hippi_stream: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response.content.response_type = HIPPI_STREAM_RESPONSE;
  
  if (debug > 2) {
    fprintf(where,"recv_hippi_stream: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */
  
  if (debug > 1) {
    fprintf(where,"recv_hippi_stream: requested alignment of %d\n",
	    hippi_stream_request->recv_alignment);
    fflush(where);
  }

  if (recv_width == 0) recv_width = 1;

  recv_ring = allocate_buffer_ring(recv_width,
				   hippi_stream_request->message_size,
				   hippi_stream_request->recv_alignment,
				   hippi_stream_request->recv_offset);

  if (debug > 1) {
    fprintf(where,"recv_hippi_stream: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* create_hippi_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */

  loc_recv_bufs = hippi_stream_request->rem_recv_bufs;
  recv_flow_control = hippi_stream_request->recv_flow_control;
  rem_hippi_sap = hippi_stream_request->client_sap;
  loc_hippi_sap = hippi_stream_request->server_sap;

  strncpy(loc_hippi_device,
	  hippi_stream_request->hippi_device,
	  hippi_stream_request->dev_name_len+1); /* we want the null too */
    
  s_data = create_hippi_socket();
  
  if (s_data < 0) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }
  
  /* return out local addresses to the remote */
  
  hippi_stream_response->test_length = hippi_stream_request->test_length;
  hippi_stream_response->server_sap = loc_hippi_sap;
  memcpy(hippi_stream_response->mac_addr,loc_hippi_mac,6);

  netperf_response.content.serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */
  
  hippi_stream_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (hippi_stream_request->measure_cpu) {
    /* We will pass the rate into the calibration routine. If the */
    /* user did not specify one, it will be 0.0, and we will do a */
    /* "real" calibration. Otherwise, all it will really do is */
    /* store it away... */
    hippi_stream_response->measure_cpu = 1;
    hippi_stream_response->cpu_rate = 
      calibrate_local_cpu(hippi_stream_request->cpu_rate);
  }
  
  message_size	= hippi_stream_request->message_size;
  test_time	= hippi_stream_request->test_length;
  
  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */

  send_response();
  
  /* there is no equivalent to listen or accept in lla... */

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(hippi_stream_request->measure_cpu);
  
  /* The loop will exit when the timer pops, or if we happen to recv a */
  /* message of less than send_size bytes... */
  
  times_up = 0;
  signal(SIGALRM,catcher);

  start_timer(test_time + PAD_TIME);
  
  if (debug) {
    fprintf(where,"recv_hippi_stream: about to enter inner sanctum.\n");
    fflush(where);
  }
  
  while (!times_up) {
    if ((len = read(s_data, 
		    recv_ring->buffer_ptr,
		    message_size)) != message_size) {
      if ((len == -1) && (errno != EINTR)) {
	netperf_response.content.serv_errno = errno;
	send_response();
	exit(1);
      }
      break;
    }
    messages_recvd++;
    recv_ring = recv_ring->next;
  }
  
  if (debug) {
    fprintf(where,"recv_hippi_stream: got %d messages.\n",messages_recvd);
    fflush(where);
  }
  
  /* The loop now exits due timer or < send_size bytes received. */
  cpu_stop(hippi_stream_request->measure_cpu,&elapsed_time);
  
  if (times_up) {
    /* we ended on a timer, subtract the PAD_TIME */
    elapsed_time -= (float)PAD_TIME;
  }
  else {
    alarm(0);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_hippi_stream: test ended in %f seconds.\n",elapsed_time);
    fflush(where);
  }
  
  
  /* We will count the "off" message that got us out of the loop */
  bytes_received = (messages_recvd * message_size) + len;
  
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_hippi_stream: got %d bytes\n",
	    bytes_received);
    fflush(where);
  }
  
  netperf_response.content.response_type	= HIPPI_STREAM_RESULTS;
  hippi_stream_results->bytes_received	= bytes_received;
  hippi_stream_results->messages_recvd	= messages_recvd;
  hippi_stream_results->elapsed_time	= elapsed_time;
  if (hippi_stream_request->measure_cpu) {
    hippi_stream_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  else {
    hippi_stream_results->cpu_util	= -1.0;
  }
  
  if (debug > 1) {
    fprintf(where,
	    "recv_hippi_stream: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

int send_hippi_rr(remote_host)
     char	remote_host[];
{
  
  char *tput_title = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  Trans.\n\
Send   Recv   Size     Size    Time     Rate         \n\
bytes  Bytes  bytes    bytes   secs.    per sec   \n\n";
  
  char *tput_fmt_0 =
    "%7.2f\n";
  
  char *tput_fmt_1_line_1 = "\
%-6d %-6d %-6d   %-6d  %-6.2f   %7.2f   \n";
  char *tput_fmt_1_line_2 = "\
%-6d %-6d\n";
  
  char *cpu_title = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Trans.   CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    Rate     local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per sec  %%      %%      us/Tr   us/Tr\n\n";
  
  char *cpu_fmt_0 =
    "%6.3f\n";
  
  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f   %-6.2f %-6.2f %-6.3f  %-6.3f\n";
  
  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";
  
  char *ksink_fmt = "\
Alignment      Offset\n\
Local  Remote  Local  Remote\n\
Send   Recv    Send   Recv\n\
%5d  %5d   %5d  %5d";
  
  int	send_socket;
  struct fis arg;

  float			elapsed_time;
  
  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  int	len;
  int	nummessages;
  int	trans_remaining;
  int	bytes_xferd;
  
  int	rsp_bytes_recvd;
  
  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;
  float	thruput;
  
#ifdef INTERVALS
  /* timing stuff */
#define	MAX_KEPT_TIMES	1024
  int	time_index = 0;
  int	unused_buckets;
  int	kept_times[MAX_KEPT_TIMES];
  int	sleep_usecs;
  unsigned	int	total_times=0;
  struct	timezone	dummy_zone;
  struct	timeval		send_time;
  struct	timeval		recv_time;
  struct	timeval		sleep_timeval;
#endif
  
  struct        sigaction       action;

  struct	hippi_rr_request_struct	*hippi_rr_request;
  struct	hippi_rr_response_struct	*hippi_rr_response;
  struct	hippi_rr_results_struct	*hippi_rr_result;
  
  init_test_vars();

  hippi_rr_request  =
    (struct hippi_rr_request_struct *)netperf_request.content.test_specific_data;
  hippi_rr_response =
    (struct hippi_rr_response_struct *)netperf_response.content.test_specific_data;
  hippi_rr_result	 =
    (struct hippi_rr_results_struct *)netperf_response.content.test_specific_data;
  
  /* we want to zero out the times, so we can detect unused entries. */
#ifdef INTERVALS
  time_index = 0;
  while (time_index < MAX_KEPT_TIMES) {
    kept_times[time_index] = 0;
    time_index += 1;
  }
  time_index = 0;
#endif
  
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */
  
  if ( print_headers ) {
    fprintf(where,"HIPPI REQUEST/RESPONSE TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      fprintf(where,cpu_title,format_units());
    else
      fprintf(where,tput_title,format_units());
  }
  
  /* initialize a few counters */
  
  nummessages	=	0;
  bytes_xferd	=	0;
  times_up 	= 	0;
  
  /* set-up the data buffers with the requested alignment and offset */

  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  send_ring = allocate_buffer_ring(send_width,
				   req_size,
				   local_send_align,
				   local_send_offset);

  recv_ring = allocate_buffer_ring(recv_width,
				   rsp_size,
				   local_recv_align,
				   local_recv_offset);

  /*set up the data socket                        */
  send_socket = create_hippi_socket();
  
  if (send_socket < 0){
    perror("netperf: send_hippi_rr: hippi rr data socket");
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"send_hippi_rr: send_socket obtained...\n");
  }
  
  /* If the user has requested cpu utilization measurements, we must */
  /* calibrate the cpu(s). We will perform this task within the tests */
  /* themselves. If the user has specified the cpu rate, then */
  /* calibrate_local_cpu will return rather quickly as it will have */
  /* nothing to do. If local_cpu_rate is zero, then we will go through */
  /* all the "normal" calibration stuff and return the rate back. If */
  /* there is no idle counter in the kernel idle loop, the */
  /* local_cpu_rate will be set to -1. */
  
  if (local_cpu_usage) {
    local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
  }
  
  /* Tell the remote end to do a listen. The server alters the socket */
  /* paramters on the other side at this point, hence the reason for */
  /* all the values being passed in the setup message. If the user did */
  /* not specify any of the parameters, they will be passed as 0, which */
  /* will indicate to the remote that no changes beyond the system's */
  /* default should be used. Alignment is the exception, it will */
  /* default to 8, which will be no alignment alterations. */
  
  netperf_request.content.request_type	  =	DO_HIPPI_RR;
  hippi_rr_request->num_recv_bufs  =	rem_recv_bufs;
  hippi_rr_request->recv_alignment =	remote_recv_align;
  hippi_rr_request->recv_offset	  =	remote_recv_offset;
  hippi_rr_request->send_alignment =	remote_send_align;
  hippi_rr_request->send_offset	  =	remote_send_offset;
  hippi_rr_request->request_size	  =	req_size;
  hippi_rr_request->response_size  =	rsp_size;
  hippi_rr_request->measure_cpu	  =	remote_cpu_usage;
  hippi_rr_request->cpu_rate	  =	remote_cpu_rate;
  if (test_time) {
    hippi_rr_request->test_length  =	test_time;
  }
  else {
    hippi_rr_request->test_length  =	test_trans * -1;
  }
  hippi_rr_request->dev_name_len   =       strlen(rem_hippi_device);
  strcpy(hippi_rr_request->hippi_device,rem_hippi_device);
  hippi_rr_request->recv_flow_control = recv_flow_control;
  hippi_rr_request->client_sap      = loc_hippi_sap;
  hippi_rr_request->server_sap      = rem_hippi_sap;
  memcpy(hippi_rr_request->mac_addr,loc_hippi_mac,6);
  if (debug > 1) {
    fprintf(where,
	    "requesting HIPPI request/response test\n");
    fflush(where);
  }
  
  send_request();
  
  /* The response from the remote will contain all of the relevant 	*/
  /* socket parameters for this test type. We will put them back into 	*/
  /* the variables here so they can be displayed if desired.  The	*/
  /* remote will have calibrated CPU if necessary, and will have done	*/
  /* all the needed set-up we will have calibrated the cpu locally	*/
  /* before sending the request, and will grab the counter value right	*/
  /* after the connect returns. The remote will grab the counter right	*/
  /* after the accept call. This saves the hassle of extra messages	*/
  /* being sent for the HIPPI tests.					*/
  
  recv_response();
  
  if (!netperf_response.content.serv_errno) {
    if (debug)
      fprintf(where,"remote listen done.\n");
    remote_cpu_usage= hippi_rr_response->measure_cpu;
    remote_cpu_rate = hippi_rr_response->cpu_rate;
  }
  else {
    errno = netperf_response.content.serv_errno;
    perror("netperf: remote error");
    exit(1);
  }
  
  /* "connect" to the remote */
  arg.reqtype = LOG_DSAP;
  arg.vtype = INTEGERTYPE;
  arg.value.i = rem_hippi_sap;

  if (ioctl(send_socket,
	    NETCTRL,
	    &arg) != 0) {
    fprintf(where,
	    "netserver: send_hippi_rr: error binding remote sap %d: errno %d",
	    rem_hippi_sap,
	    errno);
    fflush(where);
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }
  
  if (debug) {
    fprintf(where,
	    "send_hippi_rr: DSAP bound to %d\n",
	    rem_hippi_sap);
    fflush(where);
  }

  memcpy(arg.value.s,hippi_rr_response->mac_addr,6);
  arg.reqtype = LOG_DEST_ADDR;
  arg.vtype = 6;

  if (ioctl(send_socket,
	    NETCTRL,
	    &arg) != 0) {
    fprintf(where,
	    "netserver: send_hippi_rr: could not bind remote MAC: errno %d\n",
	    errno);
    fflush(where);
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }

  if (debug) {
    fprintf(where,
	    "send_hippi_rr: remote address is "),
    fprintf(where,
	    "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
	    hippi_rr_response->mac_addr[0],
	    hippi_rr_response->mac_addr[1],
	    hippi_rr_response->mac_addr[2],
	    hippi_rr_response->mac_addr[3],
	    hippi_rr_response->mac_addr[4],
	    hippi_rr_response->mac_addr[5]);
    fflush(where);
  }
  
  /* Data Socket set-up is finished. If there were problems, either the */
  /* connect would have failed, or the previous response would have */
  /* indicated a problem. I failed to see the value of the extra */
  /* message after the accept on the remote. If it failed, we'll see it */
  /* here. If it didn't, we might as well start pumping data. */
  
  /* Set-up the test end conditions. For a request/response test, they */
  /* can be either time or transaction based. */
  
  if (test_time) {
    /* The user wanted to end the test after a period of time. */
    times_up = 0;
    trans_remaining = 0;
    signal(SIGALRM, catcher);
    start_timer(test_time);
  }
  else {
    /* The tester wanted to send a number of bytes. */
    trans_remaining = test_bytes;
    times_up = 1;
  }
  
  /* The cpu_start routine will grab the current time and possibly */
  /* value of the idle counter for later use in measuring cpu */
  /* utilization and/or service demand and thruput. */
  
  cpu_start(local_cpu_usage);
  
  /* We use an "OR" to control test execution. When the test is */
  /* controlled by time, the byte count check will always return false. */
  /* When the test is controlled by byte count, the time test will */
  /* always return false. When the test is finished, the whole */
  /* expression will go false and we will stop sending data. I think I */
  /* just arbitrarily decrement trans_remaining for the timed test, but */
  /* will not do that just yet... One other question is whether or not */
  /* the send buffer and the receive buffer should be the same buffer. */
  while ((!times_up) || (trans_remaining > 0)) {
    /* send the request */
#ifdef INTERVALS
    gettimeofday(&send_time,&dummy_zone);
#endif
    if((len=write(send_socket,
		  send_ring->buffer_ptr,
/*
		  req_size)) != req_size) {
*/
		  req_size)) < 0) {
      if (errno == EINTR) {
	/* We likely hit */
	/* test-end time. */
	break;
      }
      fprintf(where,
	      "send_hippi_rr: requested send of %d bytes, %d sent\n",
	      req_size,
	      len);
      fflush(where);
      perror("send_hippi_rr: data send error");
      exit(1);
    }
    send_ring = send_ring->next;

    /* receive the response. with HIPPI we will get it all, or nothing */
    
    if((rsp_bytes_recvd=read(send_socket,
			     recv_ring->buffer_ptr,
			     rsp_size)) != rsp_size) {
      if (errno == EINTR) {
	/* Again, we have likely hit test-end time */
	break;
      }
      perror("send_hippi_rr: data recv error");
      exit(1);
    }
    recv_ring = recv_ring->next;

#ifdef INTERVALS
    gettimeofday(&recv_time,&dummy_zone);
    
    /* now we do some arithmatic on the two timevals */
    if (recv_time.tv_usec < send_time.tv_usec) {
      /* we wrapped around a second */
      recv_time.tv_usec += 1000000;
      recv_time.tv_sec  -= 1;
    }
    
    /* and store it away */
    kept_times[time_index] = (recv_time.tv_sec - send_time.tv_sec) * 1000000;
    kept_times[time_index] += (recv_time.tv_usec - send_time.tv_usec);
    
    /* at this point, we may wish to sleep for some period of */
    /* time, so we see how long that last transaction just took, */
    /* and sleep for the difference of that and the interval. We */
    /* will not sleep if the time would be less than a */
    /* millisecond.  */
    if (interval_usecs > 0) {
      sleep_usecs = interval_usecs - kept_times[time_index];
      if (sleep_usecs > 1000) {
	/* we sleep */
	sleep_timeval.tv_sec = sleep_usecs / 1000000;
	sleep_timeval.tv_usec = sleep_usecs % 1000000;
	select(0,
	       0,
	       0,
	       0,
	       &sleep_timeval);
      }
    }
    
    /* now up the time index */
    time_index = (time_index +1)%MAX_KEPT_TIMES;
#endif
    nummessages++;          
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug > 3) {
      fprintf(where,"Transaction %d completed\n",nummessages);
      fflush(where);
    }
    
  }
  
  /* this call will always give us the elapsed time for the test, and */
  /* will also store-away the necessaries for cpu utilization */
  
  cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being measured? */
  /* how long did we really run? */
  
  /* Get the statistics from the remote end. The remote will have */
  /* calculated service demand and all those interesting things. If it */
  /* wasn't supposed to care, it will return obvious values. */
  
  recv_response();
  if (!netperf_response.content.serv_errno) {
    if (debug)
      fprintf(where,"remote results obtained\n");
  }
  else {
    errno = netperf_response.content.serv_errno;
    perror("netperf: remote error");
    fprintf(stderr,"        the errno was: %d\n",
	    errno);
    fflush(where);
    exit(1);
  }

  /* The test is over. */
  
  if (close(send_socket) != 0) {
    /* we will not consider this a fatal error. just display a message */
    /* and move on */
    perror("netperf: cannot shutdown hippi socket");
  }
  

  /* We now calculate what our thruput was for the test. In the future, */
  /* we may want to include a calculation of the thruput measured by */
  /* the remote, but it should be the case that for a HIPPI stream test, */
  /* that the two numbers should be *very* close... We calculate */
  /* bytes_sent regardless of the way the test length was controlled. */
  /* If it was time, we needed to, and if it was by bytes, the user may */
  /* have specified a number of bytes that wasn't a multiple of the */
  /* send_size, so we really didn't send what he asked for ;-) We use */
  
  bytes_xferd	= (req_size * nummessages) + (rsp_size * nummessages);
  thruput		= calc_thruput(bytes_xferd);
  
  if (local_cpu_usage || remote_cpu_usage) {
    /* We must now do a little math for service demand and cpu */
    /* utilization for the system(s) */
    /* Of course, some of the information might be bogus because */
    /* there was no idle counter in the kernel(s). We need to make */
    /* a note of this for the user's benefit...*/
    if (local_cpu_usage) {
      if (local_cpu_rate == 0.0) {
	fprintf(where,"WARNING WARNING WARNING  WARNING WARNING WARNING  WARNING!\n");
	fprintf(where,"Local CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      local_cpu_utilization = calc_cpu_util(0.0);
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      local_service_demand  = calc_service_demand((double) nummessages*1024,
						  0.0,
						  0.0,
						  0);
    }
    else {
      local_cpu_utilization	= -1.0;
      local_service_demand	= -1.0;
    }
    
    if (remote_cpu_usage) {
      if (remote_cpu_rate == 0.0) {
	fprintf(where,"DANGER  DANGER  DANGER    DANGER  DANGER  DANGER    DANGER!\n");
	fprintf(where,"Remote CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      remote_cpu_utilization = hippi_rr_result->cpu_util;
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      remote_service_demand  = calc_service_demand((double) nummessages*1024,
						   0.0,
						   remote_cpu_utilization,
						   hippi_rr_result->num_cpus);
    }
    else {
      remote_cpu_utilization = -1.0;
      remote_service_demand  = -1.0;
    }
    
    /* We are now ready to print all the information. If the user */
    /* has specified zero-level verbosity, we will just print the */
    /* local service demand, or the remote service demand. If the */
    /* user has requested verbosity level 1, he will get the basic */
    /* "streamperf" numbers. If the user has specified a verbosity */
    /* of greater than 1, we will display a veritable plethora of */
    /* background information from outside of this block as it it */
    /* not cpu_measurement specific...  */
    
    switch (verbosity) {
    case 0:
      if (local_cpu_usage) {
	fprintf(where,
		cpu_fmt_0,
		local_service_demand);
      }
      else {
	fprintf(where,
		cpu_fmt_0,
		remote_service_demand);
      }
      break;
    case 1:
    case 2:
      fprintf(where,
	      cpu_fmt_1_line_1,		/* the format string */
	      loc_recv_bufs,		/* local sendbuf size */
	      rem_recv_bufs,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* guess */
	      elapsed_time,		/* how long was the test */
	      nummessages/elapsed_time,
	      local_cpu_utilization,	/* local cpu */
	      remote_cpu_utilization,	/* remote cpu */
	      local_service_demand,	/* local service demand */
	      remote_service_demand);	/* remote service demand */
      fprintf(where,
	      cpu_fmt_1_line_2,
	      loc_recv_bufs,
	      rem_recv_bufs);
      break;
    }
  }
  else {
    /* The tester did not wish to measure service demand. */
    switch (verbosity) {
    case 0:
      fprintf(where,
	      tput_fmt_0,
	      nummessages/elapsed_time);
      break;
    case 1:
    case 2:
      fprintf(where,
	      tput_fmt_1_line_1,	/* the format string */
	      loc_recv_bufs,
	      loc_recv_bufs,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* how large were the responses */
	      elapsed_time, 		/* how long did it take */
	      nummessages/elapsed_time);
      fprintf(where,
	      tput_fmt_1_line_2,
	      rem_recv_bufs, 		/* remote recvbuf size */
	      rem_recv_bufs);
      
      break;
    }
  }
  
  /* it would be a good thing to include information about some of the */
  /* other parameters that may have been set for this test, but at the */
  /* moment, I do not wish to figure-out all the  formatting, so I will */
  /* just put this comment here to help remind me that it is something */
  /* that should be done at a later time. */
  
  if (verbosity > 1) {
    /* The user wanted to know it all, so we will give it to him. */
    /* This information will include as much as we can find about */
    /* HIPPI statistics, the alignments of the sends and receives */
    /* and all that sort of rot... */
    
#ifdef INTERVALS
    kept_times[MAX_KEPT_TIMES] = 0;
    time_index = 0;
    while (time_index < MAX_KEPT_TIMES) {
      if (kept_times[time_index] > 0) {
	total_times += kept_times[time_index];
      }
      else
	unused_buckets++;
      time_index += 1;
    }
    total_times /= (MAX_KEPT_TIMES-unused_buckets);
    fprintf(where,
	    "Average response time %d usecs\n",
	    total_times);
#endif
  }
}

 /* this routine implements the receive side (netserver) of a HIPPI_RR */
 /* test. */
int 
recv_hippi_rr()
{
  
  struct ring_elt *recv_ring;
  struct ring_elt *send_ring;

  int	s_data;
  struct fis arg;

  int 	addrlen;
  int	measure_cpu;
  int	trans_received;
  int	trans_remaining;
  float	elapsed_time;
  
  struct        sigaction       action;

  struct	hippi_rr_request_struct	*hippi_rr_request;
  struct	hippi_rr_response_struct	*hippi_rr_response;
  struct	hippi_rr_results_struct	*hippi_rr_results;
  
  init_test_vars();

  hippi_rr_request  = 
    (struct hippi_rr_request_struct *)netperf_request.content.test_specific_data;
  hippi_rr_response = 
    (struct hippi_rr_response_struct *)netperf_response.content.test_specific_data;
  hippi_rr_results  = 
    (struct hippi_rr_results_struct *)netperf_response.content.test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_hippi_rr: entered...\n");
    fflush(where);
  }

  /* We want to set-up the listen socket with all the desired */
  /* parameters and then let the initiator know that all is ready. If */
  /* socket size defaults are to be used, then the initiator will have */
  /* sent us 0's. If the socket sizes cannot be changed, then we will */
  /* send-back what they are. If that information cannot be determined, */
  /* then we send-back -1's for the sizes. If things go wrong for any */
  /* reason, we will drop back ten yards and punt. */
  
  /* If anything goes wrong, we want the remote to know about it. It */
  /* would be best if the error that the remote reports to the user is */
  /* the actual error we encountered, rather than some bogus unexpected */
  /* response type message. */
  
  if (debug) {
    fprintf(where,"recv_hippi_rr: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response.content.response_type = HIPPI_RR_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_hippi_rr: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variables to be at the desired */
  /* alignments with the desired offsets. */
  
  if (debug) {
    fprintf(where,"recv_hippi_rr: requested recv alignment of %d offset %d\n",
	    hippi_rr_request->recv_alignment,
	    hippi_rr_request->recv_offset);
    fprintf(where,"recv_hippi_rr: requested send alignment of %d offset %d\n",
	    hippi_rr_request->send_alignment,
	    hippi_rr_request->send_offset);
    fflush(where);
  }

  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  recv_ring = allocate_buffer_ring(recv_width,
				   hippi_rr_request->request_size,
				   hippi_rr_request->recv_alignment,
				   hippi_rr_request->recv_offset);

  send_ring = allocate_buffer_ring(send_width,
				   hippi_rr_request->response_size,
				   hippi_rr_request->send_alignment,
				   hippi_rr_request->send_offset);

  if (debug) {
    fprintf(where,"recv_hippi_rr: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Let's clear-out our endpoints for the sake of cleanlines. Then we */
  /* can put in OUR values !-) At some point, we may want to nail this */
  /* socket to a particular network-level address. raj 8/94 */
  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_hippi_rr: grabbing a socket...\n");
    fflush(where);
  }

  /* create_hippi_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */

  strncpy(loc_hippi_device,
	  hippi_rr_request->hippi_device,
	  hippi_rr_request->dev_name_len+1); /* we want the null too */
  loc_hippi_sap = hippi_rr_request->server_sap;
  rem_hippi_sap = hippi_rr_request->client_sap;
  loc_recv_bufs = hippi_rr_request->num_recv_bufs;
  recv_flow_control = hippi_rr_request->recv_flow_control;

  s_data = create_hippi_socket();
  
  if (s_data < 0) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }

  /* We "connect" up to the remote using a couple of ioctls */
  arg.reqtype = LOG_DSAP;
  arg.vtype = INTEGERTYPE;
  arg.value.i = rem_hippi_sap;

  if (ioctl(s_data,
	    NETCTRL,
	    &arg) != 0) {
    fprintf(where,
	    "netserver: recv_hippi_rr: error binding remote sap %d: errno %d",
	    rem_hippi_sap,
	    errno);
    fflush(where);
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }
  
  memcpy(arg.value.s,hippi_rr_request->mac_addr,6);
  arg.reqtype = LOG_DEST_ADDR;
  arg.vtype = 6;

  if (ioctl(s_data,
	    NETCTRL,
	    &arg) != 0) {
    fprintf(where,
	    "netserver: recv_hippi_rr: could not bind remote MAC: errno %d\n",
	    errno);
    fflush(where);
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }

  if (debug) {
    fprintf(where,
	    "recv_hippi_rr: remote address is "),
    fprintf(where,
	    "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
	    hippi_rr_request->mac_addr[0],
	    hippi_rr_request->mac_addr[1],
	    hippi_rr_request->mac_addr[2],
	    hippi_rr_request->mac_addr[3],
	    hippi_rr_request->mac_addr[4],
	    hippi_rr_request->mac_addr[5]);
    fflush(where);
  }

  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. */
  
  memcpy(hippi_rr_response->mac_addr,loc_hippi_mac,6);
  hippi_rr_response->server_sap = loc_hippi_sap;
  netperf_response.content.serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */
  
  hippi_rr_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (hippi_rr_request->measure_cpu) {
    hippi_rr_response->measure_cpu = 1;
    hippi_rr_response->cpu_rate = 
      calibrate_local_cpu(hippi_rr_request->cpu_rate);
  }
   
  if (debug) {
    fprintf(where,"recv_hippi_rr: about to respond\n");
    fflush(where);
  }

  send_response();
  
  /* we are already "connected" */
  trans_received = 0;

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(hippi_rr_request->measure_cpu);
  
  if (hippi_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
    signal(SIGALRM, catcher);
    start_timer(hippi_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = hippi_rr_request->test_length * -1;
  }
  
  while ((!times_up) || (trans_remaining > 0)) {
    
    /* receive the request from the other side. the question remains */
    /* as to whether or not the HIPPI API provides a stream or message */
    /* paradigm. we will assume a message paradigm for the moment */
    /* raj 8/94 */
    if (read(s_data,
	     recv_ring->buffer_ptr,
	     hippi_rr_request->request_size) != 
	hippi_rr_request->request_size) {
      if (errno == EINTR) {
	/* we must have hit the end of test time. */
	break;
      }
      if (debug) {
	fprintf(where,
		"netperf: recv_hippi_rr: read: errno %d\n",
		errno);
	fflush(where);
      }
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
    }
    recv_ring = recv_ring->next;

    /* Now, send the response to the remote */
/*
    if (write(s_data,
	      send_ring->buffer_ptr,
	      hippi_rr_request->response_size) != 
	hippi_rr_request->response_size) {
*/
    if (write(s_data,
	      send_ring->buffer_ptr,
	      hippi_rr_request->response_size) < 0) {
      if (errno == EINTR) {
	/* we have hit end of test time. */
	break;
      }
      if (debug) {
	fprintf(where,
		"netperf: recv_hippi_rr: write: errno %d\n",
		errno);
	fflush(where);
      }
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
    }
    send_ring = send_ring->next;
    
    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug) {
      fprintf(where,
	      "recv_hippi_rr: Transaction %d complete.\n",
	      trans_received);
      fflush(where);
    }
    
  }
  
  
  /* The loop now exits due to timeout or transaction count being */
  /* reached */
  
  cpu_stop(hippi_rr_request->measure_cpu,&elapsed_time);
  
  if (times_up) {
    /* we ended the test by time, which was at least 2 seconds */
    /* longer than we wanted to run. so, we want to subtract */
    /* PAD_TIME from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_hippi_rr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }
  
  hippi_rr_results->bytes_received	= (trans_received * 
					   (hippi_rr_request->request_size + 
					    hippi_rr_request->response_size));
  hippi_rr_results->trans_received	= trans_received;
  hippi_rr_results->elapsed_time	= elapsed_time;
  if (hippi_rr_request->measure_cpu) {
    hippi_rr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_hippi_rr: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

void
print_hippi_usage()
{

  printf("%s",hippi_usage);
  exit(1);

}
void
scan_hippi_args(argc, argv)
     int	argc;
     char	*argv[];

{
#define HIPPI_ARGS "B:D:Fhm:M:r:s:"
  extern int	optind, opterrs;  /* index of first unused arg 	*/
  extern char	*optarg;	  /* pointer to option string	*/
  
  int		c;
  
  char	
    arg1[BUFSIZ],  /* argument holders		*/
    arg2[BUFSIZ];
  
  /* the first thing that we want to do is set all the defaults for */
  /* the test-specific parms. */

  init_test_vars();
  init_done = 1;

  /* Go through all the command line arguments and break them */
  /* out. For those options that take two parms, specifying only */
  /* the first will set both to that value. Specifying only the */
  /* second will leave the first untouched. To change only the */
  /* first, use the form "first," (see the routine break_args.. */
  
  while ((c= getopt(argc, argv, HIPPI_ARGS)) != EOF) {
    switch (c) {
    case '?':	
    case 'h':
      print_hippi_usage();
      exit(1);
    case 'B':
      /* how many receive buffers do we want allocated? */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	loc_recv_bufs = atoi(optarg);
      if (arg2[0])
	rem_recv_bufs = atoi(optarg);
      break;
    case 'D':
      /* set the hipp device file name for use in the open() call. */
      /* at some point we should do some error checking... */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	strcpy(loc_hippi_device,arg1);
      if (arg2[0])
	strcpy(rem_hippi_device,arg2);
      break;
    case 'F':
      /* we want to try for receive side flow control */
      recv_flow_control = 1;
    case 'r':
      /* set the request/response sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	req_size = atoi(arg1);
      if (arg2[0])	
	rsp_size = atoi(arg2);
      break;
    case 'm':
      /* set the send size */
      send_size = atoi(optarg);
      break;
    case 'M':
      /* set the recv size */
      recv_size = atoi(optarg);
      break;
    case 's':
      /* set the sap for the test */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	loc_hippi_sap = atoi(arg1);
      if (arg2[0])	
	rem_hippi_sap = atoi(arg2);
      break;
    };
  }
}
#endif /* DO_HIPPI */
