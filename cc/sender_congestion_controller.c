#include "sender_congestion_controller.h"

#define k_max_queue_ms		250
#define k_min_bitrate_bps	10000

sender_cc_t* sender_cc_create(void* trigger, bitrate_changed_func bitrate_cb, void* handler, pace_send_func send_cb, int queue_ms)
{
	sender_cc_t* cc = calloc(1, sizeof(sender_cc_t));
	cc->was_in_alr = -1;
	cc->accepted_queue_ms = SU_MIN(queue_ms, k_max_queue_ms);

	cc->ack = ack_estimator_create();
	cc->bwe = delay_bwe_create();
	cc->bitrate_controller = bitrate_controller_create(trigger, bitrate_cb);
	cc->pacer = pace_create(handler, send_cb);

	feedback_adapter_init(&cc->adapter);

	delay_bwe_set_min_bitrate(cc->bwe, k_min_bitrate_bps);

	return cc;
}

void sender_cc_destroy(sender_cc_t* cc)
{
	if (cc == NULL)
		return;

	if (cc->ack != NULL){
		ack_estimator_destroy(cc->ack);
		cc->ack = NULL;
	}

	if (cc->bitrate_controller != NULL){
		bitrate_controller_destroy(cc->bitrate_controller);
		cc->bitrate_controller = NULL;
	}

	if (cc->bwe != NULL){
		delay_bwe_destroy(cc->bwe);
		cc->bwe = NULL;
	}

	if (cc->pacer != NULL){
		pace_destroy(cc->pacer);
		cc->pacer = NULL;
	}

	feedback_adapter_destroy(&cc->adapter);

	free(cc);
}

void sender_cc_heartbeat(sender_cc_t* cc, int64_t now_ts)
{
	/*����pace����*/
	pace_try_transmit(cc->pacer, now_ts);

	/*���д�������*/
	bitrate_controller_heartbeat(cc->bitrate_controller, now_ts);

}

void sender_cc_add_pace_packet(sender_cc_t* cc, uint32_t packet_id, int retrans, size_t size, int64_t now_ts)
{
	pace_insert_packet(cc->pacer, packet_id, retrans, size, now_ts);
}

void sender_on_send_packet(sender_cc_t* cc, uint16_t seq, size_t size)
{
	feedback_add_packet(&cc->adapter, seq, size);

	/*todo:����RTT�������Ƿ���������������Բ�ʵ��*/
}

void sender_on_feedback(sender_cc_t* cc, bin_stream_t* strm)
{
	int i, cur_alr;
	bwe_result_t bwe_result;
	int64_t now_ts;

	if(feedback_on_feedback(&cc->adapter, strm) <= 0)
		return;

	now_ts = GET_SYS_MS();
	cur_alr = pace_get_limited_start_time(cc->pacer) > 0 ? 0 : -1;
	if (cc->was_in_alr == 0 && cur_alr != 0){
		ack_estimator_set_alrended(cc->ack, now_ts);
	}
	cc->was_in_alr = cur_alr;

	/*����Զ�˽�����������*/
	ack_estimator_incoming(cc->ack, cc->adapter.packets, cc->adapter.num);

	/*�����ӳ�״̬���з��Ͷ�ӵ�������ж�,���������µķ�������*/
	init_bwe_result_null(bwe_result);
	delay_bwe_incoming(cc->bwe, cc->adapter.packets, cc->adapter.num, ack_estimator_bitrate_bps(cc->ack), now_ts);

	/*�������ʵ���*/
	if (bwe_result.updated == 0)
		bitrate_controller_on_basedelay_result(cc->bitrate_controller, bwe_result.updated, bwe_result.probe, bwe_result.bitrate, bwe_result.recovered_from_overuse);

	/*todo:����RTT�������Ƿ���������������Բ�ʵ��*/
}

void sender_cc_update_rtt(sender_cc_t* cc, int32_t rtt)
{
	delay_bwe_rtt_update(cc->bwe, rtt);
}

void sender_cc_set_bitrates(sender_cc_t* cc, uint32_t min_bitrate, uint32_t start_bitrate, uint32_t max_bitrate)
{
	start_bitrate = SU_MIN(SU_MAX(start_bitrate, min_bitrate), max_bitrate);

	delay_bwe_set_min_bitrate(cc->bwe, min_bitrate);
	delay_bwe_set_start_bitrate(cc->bwe, start_bitrate);

	bitrate_controller_set_bitrates(cc->bitrate_controller, start_bitrate, min_bitrate, max_bitrate);
}

int64_t sender_cc_get_pacer_queue_ms(sender_cc_t* cc)
{
	return pace_queue_ms(cc->pacer);
}

int64_t sender_cc_get_first_packet_ts(sender_cc_t* cc)
{
	return cc->pacer->first_sent_ts;
}




