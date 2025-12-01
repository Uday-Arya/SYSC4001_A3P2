#!/bin/bash
set -e
mkdir -p exams
# create 20 exams. Last one has 9999
for i in $(seq -w 1 20); do
  idx=$((10#$i))
  fname="exams/exam${i}.txt"
  if [ "$idx" -eq 20 ]; then
    echo "9999" > "$fname"
  else
    # random student number 0001-9998 (avoid 9999 until last)
    num=$(( (RANDOM % 9998) + 1 ))
    printf "%04d\n" $num > "$fname"
  fi
  echo "Created $fname"
done

# Create default rubric file
cat > rubric.txt <<'EOF'
1, A
2, B
3, C
4, D
5, E
EOF

echo "Generated exams/ and rubric.txt"
