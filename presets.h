struct source {
    const String name;
    const String url;
};

// https://hendrikjansen.nl/henk/streaming.html

const source preset[]{
    { "NPO Radio1", "http://icecast.omroep.nl/radio1-bb-mp3" },
    { "NPO Radio2", "http://icecast.omroep.nl/radio2-bb-mp3" },
    { "NPO Radio2 Soul&Jazz", "http://icecast.omroep.nl/radio6-bb-mp3" },
    { "NPO 3FM", "http://icecast.omroep.nl/3fm-bb-mp3" },
    { "NPO 3FM Alternative", "http://icecast.omroep.nl/3fm-alternative-mp3" },
    { "NPO Radio4", "http://icecast.omroep.nl/radio4-bb-mp3" },
    { "NPO Radio5", "http://icecast.omroep.nl/radio5-bb-mp3" },
    { "538 Dance Department", "http://22193.live.streamtheworld.com/TLPSTR01.mp3" },
    { "Absoluut FM", "http://absoluutfm.stream.laut.fm/absoluutfm" },
    { "Amsterdam Funk Channel", "http://stream.afc.fm:8504/listen.pls" },
    { "Radio 10 Disco Classics", "http://19993.live.streamtheworld.com/RADIO10.mp3" },
    { "Sublime Soul", "http://20863.live.streamtheworld.com/SUBLIMESOUL.mp3" },
    { "XXL Stenders", "http://streams.robstenders.nl:8063/bonanza_mp3" },
    { "RadioEins", "http://rbb-edge-20b4-fra-lg-cdn.cast.addradio.de/rbb/radioeins/live/mp3/mid?ar-distributor=f0a0" },
    { "Tekno1", "http://212.83.149.66:8591" },
    { "BBC radio 1", "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_one" },
    { "BBC radio 2", "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_two" },
    { "BBC radio 3", "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_three" },
    { "BBC radio 4", "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_fourfm" },
    { "BBC radio 5", "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_five_live_online_nonuk" },
    { "BBC radio 6", "http://stream.live.vc.bbcmedia.co.uk/bbc_6music" },
};
