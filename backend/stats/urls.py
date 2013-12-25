from django.conf.urls.defaults import patterns, url

# Uncomment the next two lines to enable the admin:
# from django.contrib import admin
# admin.autodiscover()

urlpatterns = patterns('stats.views',
    # package/ubiquity/today
    # package/ubiquity/week
    # package/ubiquity/201203
    # package/ubiquity/2012
    # architecture/today
    # achitecture/week
    # /today
    # /week
    url(r'^$', 'most_common_problems_in_the_past_interval'),
    url(r'^today/$', 'today'),
    url(r'^week/$', 'week'),
    url(r'^bucket/$', 'bucket'),
    url(r'^oops/(.*)$', 'oops'),
    url(r'^common$', 'most_common_problems_in_the_past_interval'),
    url('^mean$', 'mean'),
)
