var options, a;
jQuery(function () {
	options = { serviceUrl: '/cgi-bin/suggest.cgi' };
	a = $('#query').autocomplete(options);
});
