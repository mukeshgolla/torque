#include "license_pbs.h" /* See here for the software license */
#include "net_connect.h" /* pbs_net_t */
#include "pbsd_main.h"
#include "test_pbsd_main.h"
#include <stdlib.h>
#include <stdio.h>
#include "pbs_error.h"
#include "completed_jobs_map.h"

void parse_command_line(int argc, char *argv[]);
extern bool auto_send_hierarchy;
extern completed_jobs_map_class completed_jobs_map;

START_TEST(test_parse_command_line)
  {
  char *argv[] = {strdup("pbs_server"), strdup("-n")};

  fail_unless(auto_send_hierarchy == true);
  parse_command_line(2, argv);
  fail_unless(auto_send_hierarchy == false);
  }
END_TEST

START_TEST(test_two)
  {


  }
END_TEST

Suite *pbsd_main_suite(void)
  {
  Suite *s = suite_create("pbsd_main_suite methods");
  TCase *tc_core = tcase_create("test_parse_command_line");
  tcase_add_test(tc_core, test_parse_command_line);
  suite_add_tcase(s, tc_core);

  tc_core = tcase_create("test_two");
  tcase_add_test(tc_core, test_two);
  suite_add_tcase(s, tc_core);

  return s;
  }

void rundebug()
  {
  }

int main(void)
  {
  int number_failed = 0;
  SRunner *sr = NULL;
  rundebug();
  sr = srunner_create(pbsd_main_suite());
  srunner_set_log(sr, "pbsd_main_suite.log");
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return number_failed;
  }