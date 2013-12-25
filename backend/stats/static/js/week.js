YUI().use('node', function(Y) {
Y.on('load', function() {
    // FIXME padding is entirely broken and will shave off any bars smaller
    // than it.
    var w = 75,
        h = 380,
        padding = 15;

    data.sort(function(a,b) {return parseInt(a.signature) - parseInt(b.signature);});

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
        .attr("height", function(d) { return y(d.value) - padding; });

    // Numbers on bars
    // FIXME: calculate text positioning better.
    chart.selectAll("text")
        .data(data)
      .enter().append("text")
        .attr("x", function(d, i) { return x(i) + w / 2; })
        .attr("y", function(d) { return h - y(d.value) + 30; })
        .attr("text-anchor", "middle")
        .style("stroke", "#fff")
        .text(function (d) { return d.value })
        .attr("font-size", 24)
        .attr("fill", "#fff");

    // Dates
    // FIXME figure out svg transforms to avoid absolute positioning.
    chart.append("g").selectAll("text")
        .data(data)
      .enter().append("text")
        .attr("x", function(d, i) { return x(i) + w;})
        .attr("y", h)
        .attr("dx", -w/2)
        .attr("text-anchor", "middle")
        .text(function (d) { return d.signature });

    chart.append("line")
        .attr("x1", 0)
        .attr("x2", w * data.length)
        .attr("y1", h - .5 - padding)
        .attr("y2", h - .5 - padding)
        .style("stroke", "#000");

});
});
