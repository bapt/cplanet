<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
    <head>
	<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
	<meta name="generator" content="CPlanet <?cs var:CPlanet.Version ?>" />
	<link href="index.atom" rel="alternate" title="Atom 1.0" type="application/atom+xml" />
	<link href="index.rss" rel="alternate" title="RSS 2.0" type="application/rss+xml" />
	<link rel="stylesheet" type="text/css" href="style.css" />
	<link rel="shortcut icon" href="/favicon.ico" type="image/x-icon">
	<link rel="icon" href="/favicon.ico" type="image/x-icon" />
	<title><?cs var:CPlanet.Name ?></title>
    </head>
    <body>
	<div id="contener">
	    <div id="header">
		<h1><?cs var:CPlanet.Name ?></h1>
		<h2><?cs var:CPlanet.Description ?></h2>
	    </div>
	    <div id="menu">
		<div class="logo"><img src="/etoilebsd-logo.png" alt="logo" /></div>
		<div class="menutitle">Subscriptions</div>
		<ul>
		    <?cs each:flux = CPlanet.Feed ?>
		    <li><a href="<?cs var:flux.Home ?>"> <?cs var:flux.Name ?></a></li>
		    <?cs /each ?>
		</ul>
		<div class="menutitle">Tous les flux</div>
		<ul>
			<li class="syndicate"><a class="feed" href="/cplanet.opml">OPML</a></li>
		</ul>
		<div class="menutitle">Flux</div>
		<ul>
			<li class="syndicate"><a class="feed" href="/index.rss">RSS 2.0</a></li>
			<li class="syndicate"><a class="feed" href="/index.atom">ATOM 1.0</a></li>
		</ul>
	    </div>
	    <div id="content">
		<?cs each:post = CPlanet.Posts ?>
		<br />
		<div class="date"><?cs alt:post.FormatedDate ?><cs ? var:post.Date ?><?cs /alt ?></div>
		<h2 class="storytitle"><a href="<?cs var:post.Link ?>"><?cs var:post.FeedName ?> >> <?cs var:post.Title ?></a></h2>
		<div class="comments"><?cs if:post.Author ?>Par : <?cs var:post.Author  ?> <?cs /if ?></div>
		<div class="comments">Tags: <?cs each:tags = post.Tags ?><div class="tags"><?cs var:tags.Tag ?></div> <?cs /each ?><?cs if:post.Permalink ?>| <a href="<?cs var:post.Permalink ?>">permalink</a><?cs /if ?></div>
		<?cs var:post.Description ?>
		<?cs /each ?>
	    </div>
	    <div id="footer">
		<a href="http://wiki.github.com/bapt/CPlanet">Powered by CPlanet <?cs var:CPlanet.Version ?></a>
	    </div>

	</div>
    </body>
</html>
