open_sem(){
    mkfifo pipe-$$
    exec 3<>pipe-$$
    rm pipe-$$
    local i=$1
    for((;i>0;i--)); do
        printf %s 000 >&3
    done
}

# run the given command asynchronously and pop/push tokens
run_with_lock(){
    local x
    # this read waits until there is something to read
    read -u 3 -n 3 x && ((0==x)) || exit $x
    (
     ( "$@"; )
    # push the return code of the command to the semaphore
    printf '%.3d' $? >&3
    )&
}

FILES="/datasets/cs240c-wi22-a00-public/data/Assignment2-gz/"
N=16
open_sem $N
for entry in `ls $FILES`; do
	((i=i%N)); ((i++==0)) && wait
	file="${FILES}${entry}"
	outfile="$(basename $entry .trace.gz )"
	run_with_lock ./shipmaxshct4-config2 -warmup_instructions 10000000 -simulation_instructions 100000000 -traces $file > "results/shipmaxshct4/$outfile.out" &
#	run_with_lock ./red_64kb-config2 -warmup_instructions 10000000 -simulation_instructions 100000000 -traces $file > "results/red_64kb/$outfile.out" &
#	echo "${FILES}${entry}"
done
