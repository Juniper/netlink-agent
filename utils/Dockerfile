FROM scratch
COPY . /

ENTRYPOINT ["/usr/sbin/nlagent"]
CMD ["-c", "/config/nlagent.yaml", "-t", "5"]
