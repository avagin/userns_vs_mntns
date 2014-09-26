set -e
mount --make-rprivate /
mkdir -p /tmp/xxx
mount -t tmpfs xxx /tmp/xxx
mkdir /tmp/xxx/yyy
mkdir /tmp/xxx/root
echo write "FAIL" in a test file
echo FAIL > /tmp/xxx/yyy/zzz
echo mount a tmpfs over the test file
mount -t tmpfs yyy /tmp/xxx/yyy
mount -t tmpfs root /tmp/xxx/root
echo Check that the test files is unreachable
ls -l /tmp/xxx/yyy/zzz || true
echo "Let's fight"
ZDTM_ROOT=/tmp/xxx/root ./test_userns
