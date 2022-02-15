log_csv=./results/$1_metrics.csv ;
`rm -rf $log_csv` ;
`echo "TRACE,IPC,LLC_ACCESS,LLC_MISS,LLC_PREFETCH_ACCESS,LLC_PREFETCH_MISS,LLC_WRITEBACK_ACESS,LLC_WRITEBACK_MISS" >> $log_csv`;
# `echo "\n" >> $log_csv`;
for results in results/$1/* ;
do 
    
    IPC=`grep "CPU 0 cummulative IPC" $results | cut '-d:' '-f2' | xargs | cut '-d ' '-f1'` ;

    LLC_ACCESS=`grep "LLC TOTAL" $results | cut '-d:' '-f2' | xargs | cut '-d ' '-f1'` ;
    
    LLC_MISS=`grep "LLC TOTAL" $results | cut '-d:' '-f4' | xargs` ;

    LLC_PREFETCH_ACCESS=`grep "LLC PREFETCH" $results | cut '-d:' '-f2' | xargs | cut '-d ' '-f1'` ;
    
    LLC_PREFETCH_MISS=`grep "LLC PREFETCH" $results | cut '-d:' '-f4' | xargs` ;

    LLC_WRITEBACK_ACCESS=`grep "LLC WRITEBACK" $results | cut '-d:' '-f2' | xargs | cut '-d ' '-f1'` ;

    LLC_WRITEBACK_MISS=`grep "LLC WRITEBACK" $results | cut '-d:' '-f4' | xargs` ;

    results=${results##*/}
    echo "${results%%.*},$IPC,$LLC_ACCESS,$LLC_MISS,$LLC_PREFETCH_ACCESS,$LLC_PREFETCH_MISS,$LLC_WRITEBACK_ACCESS,$LLC_WRITEBACK_MISS" >> $log_csv;

done
