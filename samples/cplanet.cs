<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
    <head>
	<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
	<meta name="generator" content="CPlanet <?cs var:CPlanet.Version ?>" />
	<link href="cplanet.atom" rel="alternate" title="Atom 1.0" type="application/atom+xml" />
	<link href="cplanet.rss" rel="alternate" title="RSS 2.0" type="application/rss+xml" />
	<link rel="stylesheet" type="text/css" href="style.css" />
	<title><?cs var:CPlanet.Name ?></title>
    </head>
    <body>
	<div id="contener">
	    <div id="header">
		<h1><?cs var:CPlanet.Name ?></h1>
		<h2><?cs var:CPlanet.Description ?></h2>
	    </div>
	    <div id="menu">
		<h2>Subscriptions</h2>
		<ul>
		    <?cs each:flux = CPlanet.Feed ?>
		    <li><a href="<?cs var:flux.Home ?>"> <?cs var:flux.Name ?></a></li>
		    <?cs /each ?>
		</ul>
		<h2>Tous les flux</h2>
		<ul>
			<li class="syndicate"><a class="feed" href="/cplanet.opml">OPML</a></li>
		</ul>
		<h2>Flux</h2>
		<ul>
			<li class="syndicate"><a class="feed" href="/index.rss">RSS 2.0</a></li>
			<li class="syndicate"><a class="feed" href="/index.atom">ATOM 1.0</a></li>
		</ul>
	    </div>
	    <div id="content">
		<?cs each:post = CPlanet.Posts ?>
		<br />
		<div id="date"><?cs alt:post.FormatedDate ?><cs ? var:post.Date ?><?cs /alt ?></div>
		<p>
		<h2 id="story-title"><a href="<?cs var:post.Link ?>"><?cs var:post.FeedName ?> >> <?cs var:post.Title ?></a></h2>
		<div id="comments"><?cs if:post.Author ?>Par : <?cs var:post.Author  ?> <?cs /if ?></div>
		<div id="comments">Tags: <?cs each:tags = post.Tags ?><div id="tags"><?cs var:tags.Tag ?></div> <?cs /each ?><?cs if:post.Permalink ?>| <a href="<?cs var:post.Permalink ?>">permalink</a><?cs /if ?></div>
		<p><?cs var:post.Description ?></p>
		</p>
		<?cs /each ?>
	    </div>
	    <div id="footer">
		<a href="http://wiki.github.com/bapt/CPlanet">Powered by CPlanet <?cs var:CPlanet.Version ?></a>
	    </div>

	</div>
    </body>
</html>
