<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
	<title type="text"><?cs var:CPlanet.Name ?></title>
	<subtitle type="text"><?cs var:CPlanet.Description ?></subtitle>
	<id><?cs var:CPlanet.URL ?>/</id>
	<link rel="self" type="text/xml" href="<?cs var:CPlanet.URL ?>/cplanet.atom" />
	<link rel="alternate" type="text/html" href="<?cs var:CPlanet.URL ?>" />
	<updated><?cs var:CPlanet.GenerationDate ?></updated>
	<author><name><?cs var:CPlanet.Name ?></name></author>
	<generator uri="http://wiki.github.com/bapt/CPlanet" version="<?cs var:CPlanet.Version ?>">CPlanet</generator>
	<?cs each:post = CPlanet.Posts ?>
	<entry>
		<title type="text"><?cs var:post.FeedName ?> >> <?cs var:post.Title ?></title>
		<summary type="text">None Currently</summary>
		<?cs if:post.Author ?><author><name> <?cs var:post.Author  ?></name></author> <?cs /if ?>
		<content type="html"><![CDATA[<?cs var:post.Description ?>]]></content>
		<?cs each:tags = post.Tags ?><category term="<?cs var:tags.Tag ?>" /><?cs /each ?>
		<id><?cs var:post.Link ?></id>
		<link rel="alternate" href="<?cs var:post.Link ?>" />
		<published><?cs var:post.Date ?></published>
	</entry>
	<?cs /each ?>
</feed>
