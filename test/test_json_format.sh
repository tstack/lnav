#! /bin/bash

lnav_test="${top_builddir}/src/lnav-test"

run_test env TZ=UTC ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_journald.json

check_output "journald json log format is not working" <<EOF
2018-08-21T14:04:21.221 38708007 medusa-GpsLocator.service python[184] FATAL GPS Reference longitude: 7.358143333
2018-08-21T14:04:21.221 38708007 medusa-GpsLocator.service python[184] INFO GPS Reference latitude: 46.908706667
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json.json

check_output "json log format is not working" <<EOF
2013-09-06T20:00:48.124 TRACE trace test
2013-09-06T20:00:49.124 INFO Starting up service
2013-09-06T22:00:49.124 INFO Shutting down service
  user: steve@example.com
2013-09-06T22:00:59.124 DEBUG5 Details...
2013-09-06T22:00:59.124 DEBUG4 Details...
2013-09-06T22:00:59.124 DEBUG3 Details...
2013-09-06T22:00:59.124 DEBUG2 Details...
2013-09-06T22:00:59.124 DEBUG Details...
2013-09-06T22:01:49.124 STATS 1 beat per second
2013-09-06T22:01:49.124 WARNING not looking good
2013-09-06T22:01:49.124 ERROR looking bad
2013-09-06T22:01:49.124 CRITICAL sooo bad
2013-09-06T22:01:49.124 FATAL shoot
  obj: { "field1" : "hi", "field2": 2 }
  arr: ["hi", {"sub1": true}]
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} -c ':switch-to-view pretty' \
    ${test_dir}/logfile_json.json

check_output "json log format is not working" <<EOF
2013-09-06T20:00:48.124 TRACE trace testbork bork bork
2013-09-06T20:00:49.124 INFO Starting up servicebork bork bork
2013-09-06T22:00:49.124 INFO Shutting down servicebork bork bork
user: mailto:steve@example.com
2013-09-06T22:00:59.124 DEBUG5 Details...bork bork bork
2013-09-06T22:00:59.124 DEBUG4 Details...bork bork bork
2013-09-06T22:00:59.124 DEBUG3 Details...bork bork bork
2013-09-06T22:00:59.124 DEBUG2 Details...bork bork bork
2013-09-06T22:00:59.124 DEBUG Details...bork bork bork
2013-09-06T22:01:49.124 STATS 1 beat per secondbork bork bork
2013-09-06T22:01:49.124 WARNING not looking goodbork bork bork
2013-09-06T22:01:49.124 ERROR looking badbork bork bork
2013-09-06T22:01:49.124 CRITICAL sooo badbork bork bork
2013-09-06T22:01:49.124 FATAL shootbork bork bork
obj: {
    "field1" : "hi",
    "field2": 2
}
arr: [
    "hi",
        {"sub1": true}
]
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/log.clog

check_output "multi-line-format json log format is not working" <<EOF
2016-08-03T12:06:31.009 - ;Exception initializing page context; java.lang.NoClassDefFoundError: javax/el/StaticFieldELResolver
       at org.apache.jasper.runtime.JspFactoryImpl.internalGetPageContext(JspFactoryImpl.java:172)
       at org.apache.jasper.runtime.JspFactoryImpl.getPageContext(JspFactoryImpl.java:123)
       at org.apache.jsp.errors._404_002dnot_002dfound_jsp._jspService(_404_002dnot_002dfound_jsp.java:38)
       at org.apache.jasper.runtime.HttpJspBase.service(HttpJspBase.java:111)
       at javax.servlet.http.HttpServlet.service(HttpServlet.java:731)
       at org.apache.jasper.servlet.JspServletWrapper.service(JspServletWrapper.java:411)
       at org.apache.jasper.servlet.JspServlet.serviceJspFile(JspServlet.java:473)
       at org.apache.jasper.servlet.JspServlet.service(JspServlet.java:377)
       at javax.servlet.http.HttpServlet.service(HttpServlet.java:731)
       at org.apache.catalina.core.ApplicationFilterChain.internalDoFilter(ApplicationFilterChain.java:303)
       at org.apache.catalina.core.ApplicationFilterChain.doFilter(ApplicationFilterChain.java:208)
       at collective.config.startup.DamFilter.doFilter(DamFilter.java:270)
       at org.apache.catalina.core.ApplicationFilterChain.internalDoFilter(ApplicationFilterChain.java:241)
       at org.apache.catalina.core.ApplicationFilterChain.doFilter(ApplicationFilterChain.java:208)
       at org.apache.catalina.core.ApplicationDispatcher.invoke(ApplicationDispatcher.java:748)
       at org.apache.catalina.core.ApplicationDispatcher.processRequest(ApplicationDispatcher.java:488)
       at org.apache.catalina.core.ApplicationDispatcher.doForward(ApplicationDispatcher.java:411)
       at org.apache.catalina.core.ApplicationDispatcher.forward(ApplicationDispatcher.java:338)
       at org.apache.catalina.core.StandardHostValve.custom(StandardHostValve.java:476)
       at org.apache.catalina.core.StandardHostValve.status(StandardHostValve.java:345)
       at org.apache.catalina.core.StandardHostValve.invoke(StandardHostValve.java:210)
       at org.apache.catalina.valves.ErrorReportValve.invoke(ErrorReportValve.java:103)
       at org.apache.catalina.core.StandardEngineValve.invoke(StandardEngineValve.java:116)
       at org.apache.catalina.valves.AccessLogValve.invoke(AccessLogValve.java:957)
       at org.apache.catalina.valves.RemoteIpValve.invoke(RemoteIpValve.java:683)
       at org.apache.catalina.connector.CoyoteAdapter.service(CoyoteAdapter.java:423)
       at org.apache.coyote.http11.AbstractHttp11Processor.process(AbstractHttp11Processor.java:1079)
       at org.apache.coyote.AbstractProtocol\$AbstractConnectionHandler.process(AbstractProtocol.java:620)
       at org.apache.tomcat.util.net.JIoEndpoint\$SocketProcessor.run(JIoEndpoint.java:316)
       at java.util.concurrent.ThreadPoolExecutor.runWorker(ThreadPoolExecutor.java:1142)
       at java.util.concurrent.ThreadPoolExecutor\$Worker.run(ThreadPoolExecutor.java:617)
       at org.apache.tomcat.util.threads.TaskThread\$WrappingRunnable.run(TaskThread.java:61)
       at java.lang.Thread.run(Thread.java:744)
Caused by: java.lang.ClassNotFoundException: javax.el.StaticFieldELResolver
       at org.apache.catalina.loader.WebappClassLoader.loadClass(WebappClassLoader.java:1720)
       at org.apache.catalina.loader.WebappClassLoader.loadClass(WebappClassLoader.java:1571)
       ... 33 common frames omitted

  @version: 1
  logger_name: org.apache.jasper.runtime.JspFactoryImpl
  thread_name: http-bio-0.0.0.0-8081-exec-198
  level: ERROR
  customer: foobaz
2016-08-03T12:06:31.009 - ;Exception initializing page context;
  @version: 1
  logger_name: org.apache.jasper.runtime.JspFactoryImpl
  thread_name: http-bio-0.0.0.0-8081-exec-198
  level: ERROR
  customer: foobaz
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from test_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json.json

check_output "log levels not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,arr,obj,user
0,<NULL>,2013-09-06 20:00:48.124,0,trace,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
1,<NULL>,2013-09-06 20:00:49.124,1000,info,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
2,<NULL>,2013-09-06 22:00:49.124,7200000,info,0,<NULL>,<NULL>,[],<NULL>,<NULL>,steve@example.com
4,<NULL>,2013-09-06 22:00:59.124,10000,debug5,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
5,<NULL>,2013-09-06 22:00:59.124,0,debug4,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
6,<NULL>,2013-09-06 22:00:59.124,0,debug3,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
7,<NULL>,2013-09-06 22:00:59.124,0,debug2,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
8,<NULL>,2013-09-06 22:00:59.124,0,debug,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
9,<NULL>,2013-09-06 22:01:49.124,50000,stats,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
10,<NULL>,2013-09-06 22:01:49.124,0,warning,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
11,<NULL>,2013-09-06 22:01:49.124,0,error,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
12,<NULL>,2013-09-06 22:01:49.124,0,critical,0,<NULL>,<NULL>,[],<NULL>,<NULL>,<NULL>
13,<NULL>,2013-09-06 22:01:49.124,0,fatal,0,<NULL>,<NULL>,[],"[""hi"", {""sub1"": true}]","{ ""field1"" : ""hi"", ""field2"": 2 }",<NULL>
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from test_log' \
    -c ':write-json-to -' \
    ${test_dir}/logfile_json.json

check_output "json output not working" <<EOF
[
    {
        "log_line": 0,
        "log_part": null,
        "log_time": "2013-09-06 20:00:48.124",
        "log_idle_msecs": 0,
        "log_level": "trace",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 1,
        "log_part": null,
        "log_time": "2013-09-06 20:00:49.124",
        "log_idle_msecs": 1000,
        "log_level": "info",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 2,
        "log_part": null,
        "log_time": "2013-09-06 22:00:49.124",
        "log_idle_msecs": 7200000,
        "log_level": "info",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": "steve@example.com"
    },
    {
        "log_line": 4,
        "log_part": null,
        "log_time": "2013-09-06 22:00:59.124",
        "log_idle_msecs": 10000,
        "log_level": "debug5",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 5,
        "log_part": null,
        "log_time": "2013-09-06 22:00:59.124",
        "log_idle_msecs": 0,
        "log_level": "debug4",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 6,
        "log_part": null,
        "log_time": "2013-09-06 22:00:59.124",
        "log_idle_msecs": 0,
        "log_level": "debug3",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 7,
        "log_part": null,
        "log_time": "2013-09-06 22:00:59.124",
        "log_idle_msecs": 0,
        "log_level": "debug2",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 8,
        "log_part": null,
        "log_time": "2013-09-06 22:00:59.124",
        "log_idle_msecs": 0,
        "log_level": "debug",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 9,
        "log_part": null,
        "log_time": "2013-09-06 22:01:49.124",
        "log_idle_msecs": 50000,
        "log_level": "stats",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 10,
        "log_part": null,
        "log_time": "2013-09-06 22:01:49.124",
        "log_idle_msecs": 0,
        "log_level": "warning",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 11,
        "log_part": null,
        "log_time": "2013-09-06 22:01:49.124",
        "log_idle_msecs": 0,
        "log_level": "error",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 12,
        "log_part": null,
        "log_time": "2013-09-06 22:01:49.124",
        "log_idle_msecs": 0,
        "log_level": "critical",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": null,
        "obj": null,
        "user": null
    },
    {
        "log_line": 13,
        "log_part": null,
        "log_time": "2013-09-06 22:01:49.124",
        "log_idle_msecs": 0,
        "log_level": "fatal",
        "log_mark": 0,
        "log_comment": null,
        "log_tags": null,
        "log_filters": [

        ],
        "arr": [
            "hi",
            {
                "sub1": true
            }
        ],
        "obj": {
            "field1": "hi",
            "field2": 2
        },
        "user": null
    }
]
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json2.json

check_output "timestamp-format not working" <<EOF
2013-09-06T20:00:49.124 abc 49 def     0 c.e.foo Starting up service
2013-09-06T22:00:49.124 abc 49 def     0 c.e.foo Shutting down service
  user: steve@example.com
2013-09-06T22:01:49.124 abc 49 def    10 c.e.foo looking bad
EOF

run_test ${lnav_test} -n -d /tmp/lnav.err \
    -I ${test_dir} \
    -c ';select * from json_log2' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json2.json

check_output "log levels not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,cl,user
0,<NULL>,2013-09-06 20:00:49.124,0,info,0,<NULL>,<NULL>,[],com.exmaple.foo,<NULL>
1,<NULL>,2013-09-06 22:00:49.124,7200000,info,0,<NULL>,<NULL>,[],com.exmaple.foo,steve@example.com
3,<NULL>,2013-09-06 22:01:49.124,60000,error,0,<NULL>,<NULL>,[],com.exmaple.foo,<NULL>
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":goto 2" \
    -c ":pipe-line-to sed -e 's/2013//g'" \
    -c ":switch-to-view text" \
    ${test_dir}/logfile_json.json
check_output "pipe-line-to is not working" <<EOF
-09-06T22:00:49.124 INFO Shutting down service
  user: steve@example.com

EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_nested_json.json

check_output "json log format is not working" <<EOF
2013-09-06T20:00:48.124 TRACE trace test
  @fields: { "lvl": "TRACE", "msg": "trace test"}
2013-09-06T20:00:49.124 INFO Starting up service
  @fields: { "lvl": "INFO", "msg": "Starting up service"}
2013-09-06T22:00:49.124 INFO Shutting down service
  @fields/user: steve@example.com
  @fields: { "lvl": "INFO", "msg": "Shutting down service", "user": "steve@example.com"}
2013-09-06T22:00:59.124 DEBUG5 Details...
  @fields: { "lvl": "DEBUG5", "msg": "Details..."}
2013-09-06T22:00:59.124 DEBUG4 Details...
  @fields: { "lvl": "DEBUG4", "msg": "Details..."}
2013-09-06T22:00:59.124 DEBUG3 Details...
  @fields: { "lvl": "DEBUG3", "msg": "Details..."}
2013-09-06T22:00:59.124 DEBUG2 Details...
  @fields: { "lvl": "DEBUG2", "msg": "Details..."}
2013-09-06T22:00:59.124 DEBUG Details...
  @fields: { "lvl": "DEBUG", "msg": "Details..."}
2013-09-06T22:01:49.124 STATS 1 beat per second
  @fields: { "lvl": "STATS", "msg": "1 beat per second"}
2013-09-06T22:01:49.124 WARNING not looking good
  @fields: { "lvl": "WARNING", "msg": "not looking good"}
2013-09-06T22:01:49.124 ERROR looking bad
  @fields: { "lvl": "ERROR", "msg": "looking bad"}
2013-09-06T22:01:49.124 CRITICAL sooo bad
  @fields: { "lvl": "CRITICAL", "msg": "sooo bad"}
2013-09-06T22:01:49.124 FATAL shoot
  @fields/trace#: line:1
  @fields/trace#: line:2
  @fields: { "lvl": "FATAL", "msg": "shoot", "trace": ["line:1", "line:2"]}
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from ntest_log' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_nested_json.json

check_output "log levels not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,@fields/trace#,@fields/user
0,<NULL>,2013-09-06 20:00:48.124,0,trace,0,<NULL>,<NULL>,[],<NULL>,<NULL>
2,<NULL>,2013-09-06 20:00:49.124,1000,info,0,<NULL>,<NULL>,[],<NULL>,<NULL>
4,<NULL>,2013-09-06 22:00:49.124,7200000,info,0,<NULL>,<NULL>,[],<NULL>,steve@example.com
7,<NULL>,2013-09-06 22:00:59.124,10000,debug5,0,<NULL>,<NULL>,[],<NULL>,<NULL>
9,<NULL>,2013-09-06 22:00:59.124,0,debug4,0,<NULL>,<NULL>,[],<NULL>,<NULL>
11,<NULL>,2013-09-06 22:00:59.124,0,debug3,0,<NULL>,<NULL>,[],<NULL>,<NULL>
13,<NULL>,2013-09-06 22:00:59.124,0,debug2,0,<NULL>,<NULL>,[],<NULL>,<NULL>
15,<NULL>,2013-09-06 22:00:59.124,0,debug,0,<NULL>,<NULL>,[],<NULL>,<NULL>
17,<NULL>,2013-09-06 22:01:49.124,50000,stats,0,<NULL>,<NULL>,[],<NULL>,<NULL>
19,<NULL>,2013-09-06 22:01:49.124,0,warning,0,<NULL>,<NULL>,[],<NULL>,<NULL>
21,<NULL>,2013-09-06 22:01:49.124,0,error,0,<NULL>,<NULL>,[],<NULL>,<NULL>
23,<NULL>,2013-09-06 22:01:49.124,0,critical,0,<NULL>,<NULL>,[],<NULL>,<NULL>
25,<NULL>,2013-09-06 22:01:49.124,0,fatal,0,<NULL>,<NULL>,[],line:1,<NULL>
EOF


run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ":goto 4" \
    -c ":pipe-line-to sed -e 's/2013//g'" \
    -c ":switch-to-view text" \
    ${test_dir}/logfile_nested_json.json
check_output "pipe-line-to is not working" <<EOF
-09-06T22:00:49.124 INFO Shutting down service
  @fields/user: steve@example.com
  @fields: { "lvl": "INFO", "msg": "Shutting down service", "user": "steve@example.com"}

EOF

run_test ${lnav_test} -n \
    -I ${test_dir} \
    ${test_dir}/logfile_json3.json

check_output "json log3 format is not working" <<EOF
2017-03-24T20:06:26.240 1.1.1.1 GET 200 /example/uri/5
2017-03-24T20:12:47.764 1.1.1.1 GET 500 /example/uri/5
2017-03-24T20:15:31.694 1.1.1.1 GET 400 /example/uri/5
EOF

run_test ${lnav_test} -n \
    -I ${test_dir} \
    -c ';select * from json_log3' \
    -c ':write-csv-to -' \
    ${test_dir}/logfile_json3.json

check_output "json log3 format is not working" <<EOF
log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,client_ip,details1,details2,details3,request/method,request/size,request/uri,response/status
0,<NULL>,2017-03-24 20:06:26.240,0,info,0,<NULL>,<NULL>,[],1.1.1.1,<NULL>,<NULL>,<NULL>,GET,166,/example/uri/5,200
1,<NULL>,2017-03-24 20:12:47.764,381524,critical,0,<NULL>,<NULL>,[],1.1.1.1,<NULL>,<NULL>,<NULL>,GET,166,/example/uri/5,500
2,<NULL>,2017-03-24 20:15:31.694,163930,warning,0,<NULL>,<NULL>,[],1.1.1.1,"{""foo"": ""bar""}","{""foo"": ""bar""}","{""foo"": ""bar""}",GET,166,/example/uri/5,400
EOF
