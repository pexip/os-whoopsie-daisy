YUI().use('node', function(Y) {
Y.on('load', function() {

    // TODO figure out why - 0.5 is needed.
    var w = Y.one("body").get("winWidth") / data.length - 0.5;
    if (w < 10) {
        w = 10;
    }
    var h = 380;

    data.sort(function(a,b) {return a.value - b.value});
    var x = d3.scale.linear()
        .domain([0, 1])
        .range([0, w]);

    var y = d3.scale.linear()
        .domain([0, d3.max(data, function (d) { return d.value })])
        .rangeRound([0, h]);

    var chart = d3.select(".content").append("svg")
        .attr("class", "chart")
        .attr("width", w * data.length - 1)
        .attr("height", h);

    chart.selectAll("rect")
        .data(data)
      .enter().append("rect")
        .attr("x", function(d, i) { return x(i) - .5; })
        .attr("y", function(d) { return h - y(d.value) - .5; })
        .attr("width", w)
        .attr("height", function(d) { return y(d.value); })
        .on("click", function (d,i) { window.location = "/bucket/?id=" + d.signature; })
        // Mouseovers of bucket signature, broken over multiple lines.
        .append("svg:title")
        .text(function (d) { return d.signature.replace(/:/gi,"\n"); });

    chart.append("line")
        .attr("x1", 0)
        .attr("x2", w * data.length)
        .attr("y1", h - .5)
        .attr("y2", h - .5)
        .style("stroke", "#000");

});
});
