TRACES=`ls *.trace`


for t in $TRACES; do
  echo -n "$t "
  ./vmsim $t 
done

