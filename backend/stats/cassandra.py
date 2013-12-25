import datetime
import pycassa
from pycassa.cassandra.ttypes import NotFoundException

configuration = None
try:
    import local_config as configuration
except ImportError:
    pass
if not configuration:
    import configuration

pool = pycassa.ConnectionPool(configuration.cassandra_keyspace, [configuration.cassandra_host], timeout=10)
daybucketscount_cf = pycassa.ColumnFamily(pool, 'DayBucketsCount')
buckets_cf = pycassa.ColumnFamily(pool, 'Buckets')
oops_cf = pycassa.ColumnFamily(pool, 'OOPS')
stacktrace_cf = pycassa.ColumnFamily(pool, 'Stacktrace')

def get_total_buckets_by_day ():
    """All of the buckets added to for the past seven days."""
    # TODO accept a date range. Client asks for the number of days it can
    # reasonably fit on the screen.
    date = datetime.datetime.utcnow()
    delta = datetime.timedelta(days=1)
    for i in range(7):
        count = daybucketscount_cf.get_count(date.strftime('%Y%m%d'))
        date = (date - delta)
        yield date.strftime('%Y%m%d'), count
    
def get_bucket_counts_for_today (batch_size=100, filter_less_than=5, show_failed=False):
    """The number of times each bucket has been added to today."""
    today = datetime.date.today().strftime('%Y%m%d')
    start = ''
    while True:
        result = daybucketscount_cf.get(today, column_start=start, column_count=100)
        for column, count in result.items():
            if not show_failed and column.startswith('failed'):
                continue
            # These are uninteresting and ubiquitous.
            if count >= filter_less_than:
                yield (column, count)
        # We do not want to include the end of the previous batch.
        start = column + '0'
        if len(result) < 100:
            raise StopIteration

def get_crashes_for_bucket (bucketid, limit=100):
    result = buckets_cf.get(bucketid, column_count=limit).keys()
    return result

def get_package_for_bucket (bucketid):
    '''Returns the first OOPS in the bucket and the package it occurred in.'''

    oopsid = buckets_cf.get(bucketid, column_count=1).keys()[0]
    try:
        # FIXME columns=['Package']
        return oops_cf.get(oopsid)['Package'].split()
    except KeyError:
        return ('', '')

def get_crash (oopsid):
    return oops_cf.get(oopsid)

def get_stacktrace_for_bucket (bucketid):
    SAS = 'StacktraceAddressSignature'
    TRACE = 'Stacktrace'
    for crash in get_crashes_for_bucket(bucketid, 10):
        sas = None
        try:
            sas = oops_cf.get(crash, columns=[SAS])[SAS]
        except NotFoundException:
            pass
        if not sas:
            # Probably a interpreted program crash.
            return ''
        try:
            return stacktrace_cf.get(sas, columns=[TRACE])[TRACE]
        except NotFoundException:
            pass

    # We didn't have a stack trace for any of the signatures in this set of
    # crashes.
    # TODO in the future, we should go to the next 10 crashes.
    return ''
