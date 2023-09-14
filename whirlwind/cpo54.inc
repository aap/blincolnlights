const int cpo50_stop_clock_tp7 = SF;
const int cpo48_stop_clock_tp2 = MR|MH|DV|SL|SR|CL;
const int cpo06_pc_clear_tp8_dly = SP;
const int cpo08_pc_rd_in_tp1 = SP;
const int cpo12_pc_rd_out_tp2 = PT;
const int cpo10_pc_rd_out_tp8 = SP;
const int cpo09_cr_clear_pc_rd_to_cb_tp2 = PT;
const int cpo14_add_to_pc_tp7 = PT;
const int cpo28_par_rd_out_tp5 = PT;
const int cpo40_par_rd_out_tp1 = SP;
const int cpo68_cs_clear_tp4 = PT;
const int cpo66_cs_rd_in_tp5 = PT;
const int cpo19_ss_clear_tp1 = SI|RC|CK|CP|SP|CA|CS|AD|SU|CM|SA|DM|MR|MH|DV|SL|SR|CL;
const int cpo20_ss_clear_tp4 = PT;
const int cpo17_ss_rd_in_tp25 = PT;
const int cpo18_ss_rd_out_cr_rd_in_tp3 = PT;
const int cpo61_stor_clear_tp8 = TS|AB|EX|AO;
const int cpo75_stor_clear_rt11_tp8 = TD|TA|SF;
const int cpo71_stor_par_rd_in_tp1 = TS|AB|EX|AO;
const int cpo67_stor_par_rd_in_rt11_tp1 = TD|TA|SF;
const int cpo64_stor_rd_out_tp7 = AB|EX|CA|CS|AD|SU|CM|SA|AO|DM|MR|MH|DV;
const int cpo76_stor_rd_out_par_rd_in_tp4 = PT;
const int cpo65_stor_rd_to_cb_tp7 = CK|AB|EX|CA|CS|AD|SU|CM|SA|AO|DM|MR|MH|DV;
const int cpo74_stor_rd_to_cb_tp4 = PT;
const int cpo78_add_tp2 = EX|CA|AD|CM|SA;
const int cpo05_add_tp7_dly = AB|AO;
const int cpo80_subtract_tp2 = CS|SU|DM|DV;
const int cpo46_carry_tp3 = EX|CA|CS|AD|SU|CM|SA|DM|MR|MH;
const int cpo46x_carry_tp8 = AB;
const int cpo32_arithmetic_check_tp4 = AB|EX|CA|CS|AD|SU|CM|AO|DM|SL|SR;
const int cpo34_special_add_tp4 = SA;
const int cpo26_special_carry_tp7 = CA|CS|CM;
const int cpo16_end_around_carry_tp8 = AO;
const int cpo52_roundoff_via_src_tp3 = MR|SL|SR;
const int cpo21_compare_tp7 = CP;
const int cpo70_ar_clear_tp6 = AB|EX|CP|SP|CA|CS|AD|SU|CM|SA|AO|DM|MR|MH|DV|SF;
const int cpo79_ar_rd_in_tp7 = AB|EX|CA|CS|AD|SU|CM|SA|AO|DM|MR|MH|DV;
const int cpo73_ar_rd_in_tp8 = SP|SF;
const int cpo69_ar_rd_out_tp8 = AB|EX|CA|CS|AD|SU|CM|SA|AO|DM|MR|MH|DV;
const int cpo60_ar_rd_out_tp1 = TA|SF;
const int cpo41_ac_src_clear_tp6 = AB|CA|CS|CM|AO;
const int cpo42_ac_src_clear_tp1 = MR|MH;
const int cpo42x_ac_clear_tp1_dly = EX;
const int cpo51_ac_clear_tp3 = DV;
const int cpo43_ac_rd_out_tp1 = SI|RC|TS|TD|AB|EX|AO;
const int cpo54_ac_rd_out_tp8 = CK;
const int cpo59_ac_rd_to_br_tp7 = DM|MR|MH;
const int cpo15_ac_carry_clear_tp4 = DV;
const int cpo55_ac_carry_clear_tp6 = AB|AD|SU|SA|AO|DM|MR|MH|DV|SL|SR|SF;
const int cpo44_br_clear_via_src_tp4 = MR|SL|SR|CL;
const int cpo53_br_clear_tp6 = CA|CS|CM|DM|MR|MH;
const int cpo2xx_br_rd_to_ac_tp7 = AB;
const int cpo27_check_magnitude_tp1 = CM|DM;
const int cpo22_ar_sign_check_tp1 = MR|MH|DV;
const int cpo23_ac_sign_check_tp6 = MR|MH|DV|SL|SR|SF;
const int cpo23x_ac_sign_check_tp8 = DM;
const int cpo24_product_sign_tp4_dly = MR|MH|DV|SL|SR|SF;
const int cpo03_sc_src_preset_tp4 = PT;
const int cpo02_sc_src_rd_in_tp5 = PT;
const int cpo01_sc_rd_out_tp8 = SF;
const int cpo04_add_to_sc_tp27 = SL|SR|CL;
const int cpo35_multiply_tp2 = MR|MH;
const int cpo36_divide_tp2 = DV;
const int cpo33_shift_left_tp2 = SL|CL;
const int cpo31_shift_right_tp2 = SR;
const int cpo25_point_off_tp7 = SF;
const int cpo49_ss_rd_out_cr_rd_in_tp6 = PT;
const int cpo58_cr_rd_in_tp8 = CK|AB|EX|CA|CS|AD|SU|CM|SA|AO|DM|MR|MH|DV;
const int cpo56_transfer_check_tp47 = PT;
const int cpo47_transfer_check_tp1 = SI|RD|RC|CK|AB|EX|CA|CS|AD|SU|CM|SA|AO|DM|MR|MH|DV;
const int cpo81_ms_rd_par_clear_tp3 = PT;
const int cpo84_ms_rd_par_clear_tp6 = RC|TD|TA|CK|AB|EX|CA|CS|AD|SU|CM|SA|AO|DM|MR|MH|DV|SF;
const int cpo82_ms_write_ss_clear_tp1 = TS|TD|TA|AB|EX|AO|SF;
const int cpo88_par_clear_tp8 = TS|AB|EX|AO;
const int cpo89x_par_clear_rt11_tp8 = TD|TA|SF;