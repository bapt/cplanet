<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
    <head>
	<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
	<meta name="generator" content="CPlanet" />
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
		    <?cs each:flux = CPlanet.Flux ?>
		    <li><a href="<?cs var:flux.Home ?>"> <?cs var:flux.Name ?></a></li>
		    <?cs /each ?>
		</ul>
	    </div>
	    <div id="content">
		<?cs each:post = CPlanet.Posts ?>
		<br />
		<div id="date"><?cs var:post.Date ?></div>
		<p>
		<h2 id="story-title"><a href="<?cs var:post.Link ?>"><?cs var:post.FluxName ?> >> <?cs var:post.Title ?></a></h2>
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
